
#include <string.h>
#include <poll.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "helper.h"
#include "comm.h"
#include "spawn.h"
#include "protocol.h"
#include "atomic.h"


static int _comm_thread(void *);
static int _comm_handle_net_changes(struct comm *self);
static int _comm_zalloc_arrays(struct alloc *alloc,
                               int nrwfds, int npollfds,
                               struct pollfd **pollfds,
                               struct buffer ***recvb,
                               struct buffer ***sendb);
static int _comm_zfree_arrays(struct alloc *alloc,
                              int nrwfds, int npollfds,
                              struct pollfd **pollfds,
                              struct buffer ***recvb,
                              struct buffer ***sendb);
static int _comm_initialize_arrays(struct network *net,
                                   int nrwfds, int npollfds,
                                   struct pollfd *pollfds,
                                   struct buffer **recvb,
                                   struct buffer **sendb);
static int _comm_transfer_arrays(int nrwfds1, struct pollfd *pollfds1,
                                 struct buffer **recvb1,
                                 struct buffer **sendb1,
                                 int nrwfds2, struct pollfd *pollfds2,
                                 struct buffer **recvb2,
                                 struct buffer **sendb2);
static int _comm_fill_sendb(struct comm *self);
static int _comm_fill_pollfds_events(struct comm *self);
static int _comm_accept(struct comm *self);
static int _comm_reads(struct comm *self);
static int _comm_writes(struct comm *self);
static int _comm_pull_recvb(struct comm *self, int i);
static int _comm_resize_recvb(struct comm *self, int i);
static int _copy_buffer(struct buffer_pool *bufpool,
                        struct buffer *buffer, struct buffer **copy);


int comm_ctor(struct comm *self, struct alloc *alloc,
              struct network *net, struct buffer_pool *bufpool,
              ll sendqsz, ll recvqsz)
{
	int err;

	err = queue_with_lock_ctor(&self->sendq, alloc, sendqsz);
	if (unlikely(err))
		return err;	/* queue_with_lock_ctor() reported reason. */

	err = queue_with_lock_ctor(&self->recvq, alloc, recvqsz);
	if (unlikely(err))
		goto fail1;	/* queue_with_lock_ctor() reported reason. */

	self->stop    = 0;
	self->alloc   = alloc;
	self->net     = net;
	self->bufpool = bufpool;

	/* Created on demand.
	 */
	self->nrwfds     = 0;
	self->nlistenfds = 0;
	self->npollfds   = 0;
	self->pollfds    = NULL;
	self->recvb      = NULL;
	self->sendb      = NULL;

	self->bcastb = NULL;
	self->bcastp = -1;

	err = thread_ctor(&self->thread);
	if (unlikely(err)) {
		error("struct thread constructor failed with error %d.", err);
		goto fail2;
	}

	/* Reserve channel zero for the spawn executable.
	 */
	self->channel = 1;

	return 0;

fail2:
	queue_with_lock_dtor(&self->recvq);	/* queue_with_lock_dtor() reports reason. */

fail1:
	queue_with_lock_dtor(&self->sendq);	/* queue_with_lock_dtor() reports reason. */

	return err;
}

int comm_dtor(struct comm *self)
{
	int err;

	if (unlikely(!self->stop)) {
		error("Communication thread is not stopped.");
		return -EINVAL;
	}

	err = thread_dtor(&self->thread);
	if (unlikely(err)) {
		error("struct thread destructor failed with error %d.", err);
		return err;
	}

	err = queue_with_lock_dtor(&self->sendq);
	if (unlikely(err))
		return err;	/* queue_with_lock_dtor() reports reason. */

	err = queue_with_lock_dtor(&self->recvq);
	if (unlikely(err))
		return err;	/* queue_with_lock_dtor() reports reason. */

	err = _comm_zfree_arrays(self->alloc, self->nrwfds, self->npollfds,
	                         &self->pollfds, &self->recvb, &self->sendb);
	if (unlikely(err))
		return err;	/* _comm_zfree_arrays() reports reason. */

	return 0;
}

int comm_start_processing(struct comm *self)
{
	int err;

	err = thread_start(&self->thread, _comm_thread, self);
	if (unlikely(err)) {
		error("Failed to start communication thread (error %d).", err);
		return err;
	}

	return 0;
}

int comm_stop_processing(struct comm *self)
{
	atomic_write(self->stop, 1);

	log("Stopping communication thread.");

	return 0;
}

int comm_resume_processing(struct comm *self)
{
	atomic_write(self->stop, 0);

	log("Resuming communication thread.");

	return 0;
}

/*
 * Halt (terminate) the communication thread.
 */
int comm_halt_processing(struct comm *self)
{
	int err;

	atomic_write(self->stop, 2);

	err = thread_join(&self->thread);
	if (unlikely(err)) {
		error("Failed to join communication thread (error %d).", err);
		return err;
	}

	return 0;
}

int comm_enqueue(struct comm *self, struct buffer *buffer)
{
	return queue_with_lock_enqueue(&self->sendq, (void *)buffer);
}

int comm_dequeue(struct comm *self, struct buffer **buffer)
{
	return queue_with_lock_dequeue(&self->recvq, (void **)buffer);
}

int comm_dequeue_would_succeed(struct comm *self, int *result)
{
	ll size;
	int err;

	err = queue_with_lock_size(&self->recvq, &size);
	*result = (size > 0);

	return err;
}

int comm_flush(struct comm *self)
{
	int err;
	ll size;
	struct timespec ts;

	while (1) {
		err = queue_with_lock_size(&self->sendq, &size);
		if (unlikely(err)) {
			fcallerror("queue_with_lock_size", err);
			continue;
		}
		if (0 == size)
			break;

		ts.tv_sec  = 1;
		ts.tv_nsec = 0;
		nanosleep(&ts, NULL);
	}

	/* FIXME Loop over the send buffers to make sure that
	 *       all messages have been send.
	 */
	ts.tv_sec  = 3;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	return 0;
}

int comm_resv_channel(struct comm *self, ui16 *channel)
{
	if (self->channel >= 1023)
		return -ENOMEM;

	*channel = self->channel;
	self->channel++;

	return 0;
}


static int _comm_thread(void *arg)
{
	struct comm *self = (struct comm *)arg;

	int err;
	int num;

	log("Entering _comm_thread() main loop.");

	while (1) {
		if (1 == atomic_read(self->stop)) {
			while (1 == atomic_read(self->stop))
				sched_yield();
		}

		if (2 == atomic_read(self->stop)) {
			log("Leaving _comm_thread() main loop.");
			break;
		}

		/* Modifications to net while we are polling or reading/writing
		 * from/to the file descriptors are potentially problematic. I do
		 * not feel good holding the lock for such a long time but since
		 * net updates are very rare and only occur during the setup phase
		 * I do not expect too much congestion.
		 */
		err = network_lock_acquire(self->net);
		if (unlikely(err))
			continue;

		err = _comm_handle_net_changes(self);
		if (unlikely(err))
			goto unlock;

		err = _comm_fill_sendb(self);
		if (unlikely(err))
			goto unlock;	/* _comm_fill_sendb() reports reason. */

		err = _comm_fill_pollfds_events(self);
		if (unlikely(err))
			goto unlock;

		/* TODO Make the timeout configurable.
		 */
		err = do_poll(self->pollfds, self->npollfds, 1, &num);
		if (unlikely(err))
			goto unlock;	/* do_poll() writes error(). */

		if (0 == num)
			goto unlock;

		err = _comm_accept(self);
		if (unlikely(err))
			goto unlock;

		err = _comm_reads(self);
		if (unlikely(err))
			goto unlock;

		err = _comm_writes(self);
		if (unlikely(err))
			goto unlock;

unlock:
		err = network_lock_release(self->net);
		if (unlikely(err)) {
			/* This is bad. I do not really know how to continue now.
			 * We probably will run into a deadlock at some future point
			 * if the lock is still functional.
			 */
			die();
		}
	}

	return 0;
}

/*
 * Called while holding self->net->lock.
 */
static int _comm_handle_net_changes(struct comm *self)
{
	int err;
	int nrwfds, npollfds, nlistenfds;
	struct pollfd *pollfds;
	struct buffer **recvb;
	struct buffer **sendb;

	nlistenfds = self->net->nlistenfds;
	nrwfds     = self->net->nports;

	/* Most likely nothing has changed.
	 */
	if (likely((self->nlistenfds == nlistenfds) &&
	           (self->nrwfds     == nrwfds    )))
		return 0;

	npollfds   = nlistenfds + nrwfds;

	err = _comm_zalloc_arrays(self->alloc, nrwfds, npollfds,
	                          &pollfds, &recvb, &sendb);
	if (unlikely(err))
		return err;	/* _comm_zalloc_arrays() reports reason. */

	err = _comm_initialize_arrays(self->net, nrwfds, npollfds,
	                              pollfds, recvb, sendb);
	if (unlikely(err))
		goto fail;	/* _comm_initialize_arrays() reports reason. */

	if (self->npollfds > 0) {
		err = _comm_transfer_arrays(self->nrwfds, self->pollfds,
		                            self->recvb, self->sendb,
		                            nrwfds, pollfds, recvb, sendb);
		if (unlikely(err))
			goto fail;	/* _comm_transfer_arrays() reports reason. */
	}

	err = _comm_zfree_arrays(self->alloc, self->nrwfds, self->npollfds,
	                         &self->pollfds, &self->recvb, &self->sendb);
	if (unlikely(err))
		return err;	/* _comm_zfree_arrays() reports reason. */

	self->npollfds   = npollfds;
	self->nlistenfds = nlistenfds;
	self->nrwfds     = nrwfds;
	self->pollfds    = pollfds;
	self->recvb      = recvb;
	self->sendb      = sendb;

	return 0;

fail:
	_comm_zfree_arrays(self->alloc, nrwfds, npollfds,
	                   &pollfds, &recvb, &sendb);

	return err;
}

static int _comm_zalloc_arrays(struct alloc *alloc,
                               int nrwfds, int npollfds,
                               struct pollfd **pollfds,
                               struct buffer ***recvb,
                               struct buffer ***sendb)
{
	int err;

	err = ZALLOC(alloc, (void **)pollfds, npollfds,
	             sizeof(struct pollfd), "pollfds");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	*recvb = NULL;
	*sendb = NULL;

	if (nrwfds > 0) {
		err = ZALLOC(alloc, (void **)recvb, nrwfds,
		             sizeof(void *), "recvb");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		err = ZALLOC(alloc, (void **)sendb, nrwfds,
		             sizeof(void *), "sendb");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}
	}

	return 0;
}

static int _comm_zfree_arrays(struct alloc *alloc,
                              int nrwfds, int npollfds,
                              struct pollfd **pollfds,
                              struct buffer ***recvb,
                              struct buffer ***sendb)
{
	int err;

	if (*pollfds) {
		err = ZFREE(alloc, (void **)pollfds, npollfds,
		            sizeof(struct pollfd), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	if (*recvb) {
		err = ZFREE(alloc, (void **)recvb, nrwfds,
		            sizeof(void *), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	if (*sendb) {
		err = ZFREE(alloc, (void **)sendb, nrwfds,
		            sizeof(void *), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	return 0;
}

static int _comm_initialize_arrays(struct network *net,
                                   int nrwfds, int npollfds,
                                   struct pollfd *pollfds,
                                   struct buffer **recvb,
                                   struct buffer **sendb)
{
	int i, j;

	memset(pollfds, 0, npollfds*sizeof(struct pollfd));
	memset(recvb  , 0, nrwfds  *sizeof(void *));
	memset(sendb  , 0, nrwfds  *sizeof(void *));

	for (i = 0; i < nrwfds; ++i)
		pollfds[i].fd = net->ports[i];

	for (i = nrwfds, j = 0; i < npollfds; ++i, ++j)
		pollfds[i].fd = net->listenfds[j];

	return 0;
}

static int _comm_transfer_arrays(int nrwfds1, struct pollfd *pollfds1,
                                 struct buffer **recvb1,
                                 struct buffer **sendb1,
                                 int nrwfds2, struct pollfd *pollfds2,
                                 struct buffer **recvb2,
                                 struct buffer **sendb2)
{
	int i, j;

	/* FIXME This function cannot properly handle the case where ports disappear.
	 *       In this case we forget about buffers and introduce a leak.
	 */

	for (i = 0; i < nrwfds2; ++i)
		for (j = 0; j < nrwfds1; ++j)
			if (pollfds2[i].fd == pollfds1[j].fd) {
				sendb2[i] = sendb1[j];
				recvb2[i] = recvb1[j];

				break;
			}

	return 0;
}

static int _comm_fill_sendb(struct comm *self)
{
	int i, n, err;
	struct buffer *buffer;
	struct message_header header;

	while (1) {
		if ((buffer = self->bcastb)) {
			n = 1;
			for (i = 0; i < self->nrwfds; ++i)
				n += (NULL == self->sendb[i]);

			/* Do not process normal messages until we have finished the
			 * broadcast.
			 * TODO This is not really optimal but simplifies the logic.
			 */
			if (n < self->npollfds)
				break;

			for (i = 0; i < self->nrwfds; ++i) {
				if (self->bcastp == i)
					continue;

				self->sendb[i] = buffer;
				break;
			}
			for (; i < self->nrwfds; ++i) {
				if (self->bcastp == i)
					continue;

				err = _copy_buffer(self->bufpool, buffer, &self->sendb[i]);
				if (unlikely(err)) {
					fcallerror("_copy_buffer", err);
					die();	/* FIXME? */
				}
			}

			self->bcastb = NULL;
			self->bcastp = -1;

			/* If we reached this point all send buffers are full so we might
			 * as well break.
			 */
			break;
		}

		err = queue_with_lock_peek(&self->sendq, (void **)&buffer);
		if (-ENOENT == err)
			break;
		if (unlikely(err))
			return err;

		err = secretly_copy_header(buffer, &header);
		if (unlikely(err))
			return err;

		if (MESSAGE_FLAG_BCAST & header.flags) {
			if (self->bcastb) {
				error("bcastb must be NULL at this point.");
				die();
			}

			err = queue_with_lock_dequeue(&self->sendq, (void **)&buffer);
			if (unlikely(err))
				return err;

			err = buffer_seek(buffer, 0);
			if (unlikely(err)) {
				fcallerror("buffer_seek", err);
				die();	/* Pretty much impossible anyway
					 * so why bother?
					 */
			}

			self->bcastb = buffer;
			/* This broadcast message originated from this host so we shoul
			 * not omit any ports when sending it out.
			 */
			self->bcastp = -1;

			/* Take another spin and let the broadcast logic handle the case. */
			continue;
		}

		/* MESSAGE_FLAG_UCAST & header.flags is true.
		 */

		/* Route local message directly to the receive queue.
		 */
		if (self->net->here == header.dst) {
			err = queue_with_lock_dequeue(&self->sendq, (void **)&buffer);
			if (unlikely(err))
				return err;

			err = buffer_seek(buffer, 0);
			if (unlikely(err)) {
				fcallerror("buffer_seek", err);
				die();	/* Pretty much impossible anyway
					 * so why bother?
					 */
			}

			err = queue_with_lock_enqueue(&self->recvq, buffer);
			if (unlikely(err))
				fcallerror("queue_with_lock_enqueue", err);
				/* Will cause a memory leak that we just
				 * have to live with. */

			continue;
		}

		if (unlikely((header.dst < 0) ||
		             (header.dst >= self->net->size) ||
		             (-1 == self->net->lft[header.dst]))) {

			if ((header.dst < 0) || (header.dst >= self->net->size))
				error("Dropping message with "
				      "invalid destination %d.", header.dst);
			else
				error("Dropping message with destination %d "
				      "due to missing LFT entry.", header.dst);

			err = queue_with_lock_dequeue(&self->sendq, (void **)&buffer);
			if (unlikely(err)) {
				fcallerror("queue_dequeue", err);
				return err;
			}

			continue;
		}

		if (self->sendb[self->net->lft[header.dst]])
			break;

		err = queue_with_lock_dequeue(&self->sendq, (void *)&buffer);
		if (unlikely(err))
			return err;

		/* We use the position pointer as a write water-level gauge */
		err = buffer_seek(buffer, 0);
		if (unlikely(err)) {
			fcallerror("buffer_seek", err);
			die();	/* Pretty much impossible anyway
				 * so why bother?
				 */
		}

		self->sendb[self->net->lft[header.dst]] = buffer;
	}

	return 0;
}

static int _comm_fill_pollfds_events(struct comm *self)
{
	int i;

	for (i = 0; i < self->npollfds; ++i) {
		self->pollfds[i].events = self->pollfds[i].revents = 0;
	}

	for (i = 0; i < self->nrwfds; ++i) {
		self->pollfds[i].events |= POLLIN | POLLPRI | POLLERR;

		if (self->sendb[i])
			self->pollfds[i].events |= POLLOUT;
	}

	if (-1 == atomic_read(self->net->newfd)) {
		for (i = self->nrwfds; i < self->npollfds; ++i)
			self->pollfds[i].events |= POLLIN | POLLPRI | POLLERR;
	}

	return 0;
}

static int _comm_accept(struct comm *self)
{
	int err, tmp;
	int fd;
	int i;

	if (unlikely(0 == self->npollfds))
		return -EINVAL;

	for (i = self->nrwfds; i < self->npollfds; ++i) {
		if (!(self->pollfds[i].revents & POLLIN))
			return 0;

		fd = do_accept(self->pollfds[i].fd, NULL, NULL);
		if (unlikely(fd < 0))
			return fd;

		err = cond_var_lock_acquire(&self->cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_acquire", err);
			die();
		}

		log("Accepted new connection on fd %d.", fd);

		/* Other threads will only write to newfd if newfd is not equal to -1. We tested
		 * this at the beginning of this loop iteration. If newfd is not equal to -1 it
		 * means that someone else violated the convention.
		 */
		tmp = atomic_cmpxchg(self->net->newfd, -1, fd);
		if (unlikely(-1 != tmp)) {
			error("Detected unexpected write to net->newfd.");
			die();
		}

		err = cond_var_broadcast(&self->cond);
		if (unlikely(err))
			fcallerror("lock_var_broadcast", err);

		err = cond_var_lock_release(&self->cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_release", err);
			die();
		}

		break;
	}

	return 0;
}

static int _comm_reads(struct comm *self)
{
	int err;
	int i;
	struct message_header header;

	for (i = 0; i < self->nrwfds; ++i) {
		if (!(self->pollfds[i].revents & POLLIN))
			continue;

		if (!self->recvb[i]) {
			err = _comm_pull_recvb(self, i);
			if (unlikely(err))
				continue;
		}

		err = buffer_read(self->recvb[i], self->pollfds[i].fd);
		if (unlikely(err)) {
			fcallerror("buffer_read", err);
			continue;
		}

		if (!buffer_pos_equal_size(self->recvb[i]))
			continue;

		/* The protocol asserts that the payload size
		 * is positive. If the buffer size equals the
		 * header size we know that we have only read
		 * the header. */
		if (sizeof(struct message_header) ==
		                buffer_size(self->recvb[i])) {
			err = _comm_resize_recvb(self, i);
			if (unlikely(err))
				die();		/* We probably received a malformed
						 * message. If we try to continue
						 * a lot of bad things may happen.
						 * Better to stop here. */
		} else {
			err = buffer_seek(self->recvb[i], 0);
			if (unlikely(err)) {
				fcallerror("buffer_seek", err);
				die();
			}

			err = secretly_copy_header(self->recvb[i], &header);
			if (unlikely(err))
				return err;

			/* Handle unicast routing.
			 */
			if ((MESSAGE_FLAG_UCAST & header.flags) &&
			    (self->net->here != header.dst)) {
				err = queue_with_lock_enqueue(&self->sendq, self->recvb[i]);
				if (unlikely(err))
					fcallerror("queue_with_lock_enqueue", err);
					/* Lost message. */

				self->recvb[i] = NULL;
				continue;
			}

			/* Handle broadcast routing. Only necessary if the number of ports
			 * equals at least two (i.e., npollfds equals at least 3).
			 */
			if ((MESSAGE_FLAG_BCAST & header.flags) &&
			    (self->npollfds > 2)) {
				if (self->bcastb) {
					error("Currently we can only handle one broadcast at a time.");	/* FIXME */
					die();
				}

				err = _copy_buffer(self->bufpool, self->recvb[i], &self->bcastb);
				if (unlikely(err)) {
					fcallerror("_copy_buffer", err);
					die();	/* FIXME? */
				}

				self->bcastp = i;
			}

			/* FIXME This is not very efficient. The queue is now protected by
			 *       two separate locks!
			 */

			err = cond_var_lock_acquire(&self->cond);
			if (unlikely(err)) {
				fcallerror("cond_var_lock_acquire", err);
				die();
			}

			err = queue_with_lock_enqueue(&self->recvq, self->recvb[i]);
			if (unlikely(err))
				fcallerror("queue_with_lock_enqueue", err);

			err = cond_var_broadcast(&self->cond);
			if (unlikely(err))
				fcallerror("lock_var_broadcast", err);

			err = cond_var_lock_release(&self->cond);
			if (unlikely(err)) {
				fcallerror("cond_var_lock_release", err);
				die();
			}

			self->recvb[i] = NULL;
		}
	}

	return 0;
}

static int _comm_writes(struct comm *self)
{
	int err;
	int i;

	for (i = 0; i < self->nrwfds; ++i) {
		if (!(self->pollfds[i].revents & POLLOUT))
			continue;
		if (!self->sendb[i])
			continue;

		err = buffer_write(self->sendb[i], self->pollfds[i].fd);
		if (unlikely(err)) {
			fcallerror("buffer_write", err);
			continue;
		}

		if (buffer_pos_equal_size(self->sendb[i])) {
			err = buffer_pool_push(self->bufpool, self->sendb[i]);
			if (unlikely(err))
				fcallerror("buffer_pool_push", err);
				/* Will cause a memory leak that we just
				 * have to live with. */

			self->sendb[i] = NULL;
		}
	}

	return 0;
}

static int _comm_pull_recvb(struct comm *self, int i)
{
	int err, tmp;

	err = buffer_pool_pull(self->bufpool, &self->recvb[i]);
	if (unlikely(err)) {
		fcallerror("buffer_pool_pull", err);
		return err;
	}

	err = buffer_clear(self->recvb[i]);
	if (unlikely(err)) {
		fcallerror("buffer_clear", err);
		goto fail;
	}

	err = buffer_resize(self->recvb[i],
	                    sizeof(struct message_header));
	if (unlikely(err)) {
		fcallerror("buffer_resize", err);
		goto fail;
	}

	return 0;

fail:
	tmp = buffer_pool_push(self->bufpool, self->recvb[i]);
	if (unlikely(tmp))
		fcallerror("buffer_pool_push", tmp);

	return err;
}

static int _comm_resize_recvb(struct comm *self, int i)
{
	int err;
	struct message_header header;
	ll size;

	err = secretly_copy_header(self->recvb[i], &header);
	if (unlikely(err))
		return err;

	size = sizeof(struct message_header) + header.payload;

	err = buffer_resize(self->recvb[i], size);
	if (unlikely(err)) {
		fcallerror("buffer_resize", err);
		return err;
	}

	return 0;
}

int secretly_copy_header(struct buffer *buffer,
                         struct message_header *header)
{
	int err;
	ll pos;

	pos = buffer->pos;

	err = buffer_seek(buffer, 0);
	if (unlikely(err)) {
		fcallerror("buffer_seek", err);
		return err;
	}

	err = unpack_message_header(buffer, header);
	if (unlikely(err)) {
		fcallerror("unpack_message_header", err);
		return err;
	}

	err = buffer_seek(buffer, pos);
	if (unlikely(err)) {
		fcallerror("buffer_seek", err);
		return err;
	}

	return 0;
}

static int _copy_buffer(struct buffer_pool *bufpool, struct buffer *buffer, struct buffer **copy)
{
	int err, tmp;

	err = buffer_pool_pull(bufpool, copy);
	if (unlikely(err)) {
		fcallerror("buffer_pool_pull", err);
		return err;
	}

	err = buffer_copy(*copy, buffer);
	if (unlikely(err)) {
		fcallerror("buffer_copy", err);
		goto fail;
	}

	return 0;

fail:
	tmp = buffer_pool_push(bufpool, *copy);
	if (unlikely(tmp))
		fcallerror("buffer_pool_push", tmp);

	return err;
}

