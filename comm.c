
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
static int _comm_queue_ctor(struct comm_queue *self,
                            struct alloc * alloc, ll size);
static int _comm_queue_dtor(struct comm_queue *self);
static int _comm_queue_enqueue(struct comm_queue *self,
                               struct buffer *buffer);
static int _comm_queue_dequeue(struct comm_queue *self,
                               struct buffer **buffer);
static int _comm_queue_peek(struct comm_queue *self,
                            struct buffer **buffer);
static int _comm_queue_size(struct comm_queue *self, ll *size);
static int _comm_handle_net_changes(struct comm *self);
static int _comm_zalloc_arrays(struct alloc *alloc, int npollfds,
                               struct pollfd **pollfds,
                               struct buffer ***recvb,
                               struct buffer ***sendb);
static int _comm_zfree_arrays(struct alloc *alloc, int npollfds,
                              struct pollfd **pollfds,
                              struct buffer ***recvb,
                              struct buffer ***sendb);
static int _comm_initialize_arrays(struct network *net,
                                   int npollfds, struct pollfd *pollfds,
                                   struct buffer **recvb,
                                   struct buffer **sendb);
static int _comm_transfer_arrays(int npollfds1, struct pollfd *pollfds1,
                                 struct buffer **recvb1,
                                 struct buffer **sendb1,
                                 int npollfds2, struct pollfd *pollfds2,
                                 struct buffer **recvb2,
                                 struct buffer **sendb2);
static int _comm_fill_sendb(struct comm *self);
static int _comm_fill_pollfds_events(struct comm *self);
static int _comm_accept(struct comm *self);
static int _comm_reads(struct comm *self);
static int _comm_writes(struct comm *self);
static int _comm_pull_recvb(struct comm *self, int i);
static int _comm_resize_recvb(struct comm *self, int i);
static int _secretly_copy_header(struct buffer *buffer, struct message_header *header);


int comm_ctor(struct comm *self, struct alloc *alloc,
              struct network *net, struct buffer_pool *bufpool,
              ll sendqsz, ll recvqsz)
{
	int err;

	err = _comm_queue_ctor(&self->sendq, alloc, sendqsz);
	if (unlikely(err))
		return err;	/* _comm_queue_ctor() reported reason. */

	err = _comm_queue_ctor(&self->recvq, alloc, recvqsz);
	if (unlikely(err))
		goto fail1;	/* _comm_queue_ctor() reported reason. */

	self->stop    = 0;
	self->alloc   = alloc;
	self->net     = net;
	self->bufpool = bufpool;

	/* Created on demand.
	 */
	self->npollfds = 0;
	self->pollfds  = NULL;
	self->recvb    = NULL;
	self->sendb    = NULL;

	err = thread_ctor(&self->thread);
	if (unlikely(err)) {
		error("struct thread constructor failed with error %d.", err);
		goto fail2;
	}

	return 0;

fail2:
	_comm_queue_dtor(&self->recvq);	/* _comm_queue_dtor() reports reason. */

fail1:
	_comm_queue_dtor(&self->sendq);	/* _comm_queue_dtor() reports reason. */

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

	err = _comm_queue_dtor(&self->sendq);
	if (unlikely(err))
		return err;	/* _comm_queue_dtor() reports reason. */

	err = _comm_queue_dtor(&self->recvq);
	if (unlikely(err))
		return err;	/* _comm_queue_dtor() reports reason. */

	err = _comm_zfree_arrays(self->alloc, self->npollfds,
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

	return 0;
}

int comm_resume_processing(struct comm *self)
{
	atomic_write(self->stop, 0);

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
	return _comm_queue_enqueue(&self->sendq, buffer);
}

int comm_dequeue(struct comm *self, struct buffer **buffer)
{
	return _comm_queue_dequeue(&self->recvq, buffer);
}

int comm_dequeue_would_succeed(struct comm *self, int *result)
{
	ll size;
	int err;

	err = _comm_queue_size(&self->recvq, &size);
	*result = (size > 0);

	return err;
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

		if (unlikely(self->npollfds != (1 + self->net->nports))) {
			err = _comm_handle_net_changes(self);
			if (unlikely(err))
				goto unlock;
		}

		err = _comm_fill_sendb(self);
		if (unlikely(err))
			goto unlock;	/* _comm_fill_sendb() reports reason. */

		err = _comm_fill_pollfds_events(self);
		if (unlikely(err))
			goto unlock;

		err = do_poll(self->pollfds, self->npollfds, 1, &num);	/* TODO	Make the timeout configurable. */
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

		/* FIXME Do the rest of the work! */

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

static int _comm_queue_ctor(struct comm_queue *self,
                            struct alloc *alloc, ll size)
{
	int err, tmp;

	err = lock_ctor(&self->lock);
	if (unlikely(err)) {
		error("Failed to create lock (error %d).", err);
		return err;
	}

	err = queue_ctor(&self->queue, alloc, size);
	if (unlikely(err)) {
		error("struct queue constructor failed with error %d.", err);
		goto fail;
	}

	return 0;

fail:
	assert(err);

	tmp = lock_dtor(&self->lock);
	if (unlikely(tmp))
		error("Failed to destruct lock (error %d).", tmp);

	return err;
}

static int _comm_queue_dtor(struct comm_queue *self)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_dtor(&self->queue);
	if (unlikely(err)) {
		error("struct queue destructor failed with error %d.", err);
		goto fail;
	}

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	err = lock_dtor(&self->lock);
	if (unlikely(err)) {
		error("Failed to destruct lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

static int _comm_queue_enqueue(struct comm_queue *self,
                               struct buffer *buffer)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_enqueue(&self->queue, (void *)buffer);
	if (unlikely(err))
		goto fail;

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

static int _comm_queue_dequeue(struct comm_queue *self,
                               struct buffer **buffer)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_dequeue(&self->queue, (void **)buffer);
	if (unlikely(err))
		goto fail;

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

static int _comm_queue_peek(struct comm_queue *self,
                            struct buffer **buffer)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_peek(&self->queue, (void **)buffer);
	if (unlikely(err))
		goto fail;

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

static int _comm_queue_size(struct comm_queue *self, ll *size)
{
	int err;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	queue_size(&self->queue, size);	/* No real error code. */

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;
}

/*
 * Called while holding self->net->lock.
 */
static int _comm_handle_net_changes(struct comm *self)
{
	int err;
	int npollfds;
	struct pollfd *pollfds;
	struct buffer **recvb;
	struct buffer **sendb;

	npollfds = 1 + self->net->nports;

	err = _comm_zalloc_arrays(self->alloc, npollfds,
	                          &pollfds, &recvb, &sendb);
	if (unlikely(err))
		return err;	/* _comm_zalloc_arrays() reports reason. */

	err = _comm_initialize_arrays(self->net, npollfds, pollfds,
	                              recvb, sendb);
	if (unlikely(err))
		goto fail;	/* _comm_initialize_arrays() reports reason. */

	if (self->npollfds > 0) {
		err = _comm_transfer_arrays(self->npollfds, self->pollfds,
		                            self->recvb, self->sendb,
		                            npollfds, pollfds, recvb, sendb);
		if (unlikely(err))
			goto fail;	/* _comm_transfer_arrays() reports reason. */
	}

	err = _comm_zfree_arrays(self->alloc, self->npollfds,
	                         &self->pollfds, &self->recvb, &self->sendb);
	if (unlikely(err))
		return err;	/* _comm_zfree_arrays() reports reason. */

	self->npollfds = npollfds;
	self->pollfds  = pollfds;
	self->recvb    = recvb;
	self->sendb    = sendb;

	return 0;

fail:
	_comm_zfree_arrays(self->alloc, npollfds,
	                   &pollfds, &recvb, &sendb);

	return err;
}

static int _comm_zalloc_arrays(struct alloc *alloc, int npollfds,
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

	if (unlikely(npollfds > 1)) {
		err = ZALLOC(alloc, (void **)recvb, npollfds - 1,
		             sizeof(void *), "recvb");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		err = ZALLOC(alloc, (void **)sendb, npollfds - 1,
		             sizeof(void *), "sendb");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}
	} else {
		*recvb = NULL;
		*sendb = NULL;
	}

	return 0;
}

static int _comm_zfree_arrays(struct alloc *alloc, int npollfds,
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
		if (unlikely(npollfds < 2)) {
			error("recvb is not NULL but npollfds = %d", npollfds);
			return err;
		}

		err = ZFREE(alloc, (void **)recvb, npollfds - 1,
		            sizeof(void *), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	if (*sendb) {
		if (unlikely(npollfds < 2)) {
			error("sendb is not NULL but npollfds = %d", npollfds);
			return err;
		}

		err = ZFREE(alloc, (void **)sendb, npollfds - 1,
		            sizeof(void *), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	return 0;
}

static int _comm_initialize_arrays(struct network *net,
                                   int npollfds, struct pollfd *pollfds,
                                   struct buffer **recvb,
                                   struct buffer **sendb)
{
	int i;

	memset(pollfds, 0, npollfds*sizeof(struct pollfd));
	for (i = 0; i < npollfds - 1; ++i) {
		recvb[i] = NULL;
		sendb[i] = NULL;
	}

	pollfds[0].fd = net->listenfd;

	for (i = 0; i < npollfds - 1; ++i) {
		pollfds[i + 1].fd = net->ports[i];
	}

	return 0;
}

static int _comm_transfer_arrays(int npollfds1, struct pollfd *pollfds1,
                                 struct buffer **recvb1,
                                 struct buffer **sendb1,
                                 int npollfds2, struct pollfd *pollfds2,
                                 struct buffer **recvb2,
                                 struct buffer **sendb2)
{
	int i, j;

	/* FIXME This function cannot properly handle the case where ports disappear.
	 *       In this case we forget about buffers and introduce a leak.
	 */

	for (i = 0; i < npollfds2 - 1; ++i)
		for (j = 0; j < npollfds1 -1; ++j)
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
		err = _comm_queue_peek(&self->sendq, &buffer);
		if (-ENOENT == err)
			break;
		if (unlikely(err))
			return err;

		err = _secretly_copy_header(buffer, &header);
		if (unlikely(err))
			return err;

		if (MESSAGE_FLAG_BCAST & header.flags) {
			/* FIXME This is not really optimal. We are draining all
			 *	 buffers before processing the broadcast message.
			 */

			n = 1;
			for (i = 0; i < self->npollfds - 1; ++i)
				n += (NULL == self->sendb[i]);

			if (n != self->npollfds)
				break;
		} else {
			if (unlikely((header.dst < 0) ||
			             (header.dst >= self->net->size) ||
			             (-1 == self->net->lft[header.dst]))) {
				error("Dropping invalid message " \
				      "with destination %d.", header.dst);

				err = _comm_queue_dequeue(&self->sendq, &buffer);
				if (unlikely(err)) {
					fcallerror("queue_dequeue", err);
					return err;
				}

				continue;
			}

			if (self->sendb[self->net->lft[header.dst]])
				break;
		}

		err = _comm_queue_dequeue(&self->sendq, (void *)&buffer);
		if (unlikely(err))
			return err;

		/* We use the position pointer as a write */
		err = buffer_seek(buffer, 0);
		if (unlikely(err)) {
			fcallerror("buffer_seek", err);
			die();	/* Pretty much impossible anyway
				 * so why bother?
				 */
		}

		if (MESSAGE_FLAG_BCAST & header.flags) {
			for (i = 0; i < self->npollfds - 1; ++i)
				self->sendb[i] = buffer;
		} else {
			self->sendb[self->net->lft[header.dst]] = buffer;
		}
	}

	return 0;
}

static int _comm_fill_pollfds_events(struct comm *self)
{
	int i;

	for (i = 0; i < self->npollfds; ++i) {
		self->pollfds[i].events = self->pollfds[i].revents = 0;
	}

	if (-1 == atomic_read(self->net->newfd))
		self->pollfds[0].events |= POLLIN | POLLPRI | POLLERR;

	for (i = 1; i < self->npollfds; ++i) {
		self->pollfds[i].events |= POLLIN | POLLPRI | POLLERR;
	}

	for (i = 0; i < self->npollfds - 1; ++i) {
		if (self->sendb[i])
			self->pollfds[i+1].events |= POLLOUT;
	}

	return 0;
}

static int _comm_accept(struct comm *self)
{
	int tmp;
	int fd;

	if (unlikely(0 == self->npollfds))
		return -EINVAL;

	if (!(self->pollfds[0].revents & POLLIN))
		return 0;

	fd = do_accept(self->pollfds[0].fd, NULL, NULL);
	if (unlikely(fd < 0))
		return fd;

	log("Accepted new connection on fd %d.", fd);

	/* Other threads will only write to newfd if newfd is not equal to -1. We tested
	 * this at the beginning of this loop iteration. If newfd is not equal to -1 it
	 * means that someone else violated the convention and
	 */
	tmp = atomic_cmpxchg(self->net->newfd, -1, fd);
	if (unlikely(-1 != tmp)) {
		error("Detected unexpected write to net->newfd.");
		die();
	}

	return 0;
}

static int _comm_reads(struct comm *self)
{
	int err;
	int i;

	for (i = 0; i < self->npollfds - 1; ++i) {
		if (!(self->pollfds[i+1].revents & POLLIN))
			continue;

		if (!self->recvb[i]) {
			err = _comm_pull_recvb(self, i);
			if (unlikely(err))
				continue;
		}

		err = buffer_read(self->recvb[i], self->pollfds[i+1].fd);
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
			/* FIXME We still need to handle routing here. */

			err = buffer_seek(self->recvb[i], 0);
			if (unlikely(err)) {
				fcallerror("buffer_seek", err);
				die();
			}

			/* FIXME This is not very efficient. The queue is now protected by
			 *       two separate locks!
			 */

			err = cond_var_lock_acquire(&self->cond);
			if (unlikely(err)) {
				fcallerror("cond_var_lock_acquire", err);
				die();
			}

			err = _comm_queue_enqueue(&self->recvq, self->recvb[i]);
			if (unlikely(err))
				fcallerror("_comm_queue_enqueue", err);
				/* Will cause a memory leak that we just
				 * have to live with. */

			self->recvb[i] = NULL;

			err = cond_var_broadcast(&self->cond);
			if (unlikely(err))
				fcallerror("lock_var_broadcast", err);

			err = cond_var_lock_release(&self->cond);
			if (unlikely(err)) {
				fcallerror("cond_var_lock_release", err);
				die();
			}
		}
	}

	return 0;
}

static int _comm_writes(struct comm *self)
{
	int err;
	int i;

	for (i = 0; i < self->npollfds - 1; ++i) {
		if (!(self->pollfds[i+1].revents & POLLOUT))
			continue;
		if (!self->sendb[i])
			continue;

		err = buffer_write(self->sendb[i], self->pollfds[i+1].fd);
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

	err = _secretly_copy_header(self->recvb[i], &header);
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

static int _secretly_copy_header(struct buffer *buffer, struct message_header *header)
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

	/* Check protocol conformity.
	 */
	if (unlikely(header->payload < 1)) {
		error("Invalid payload size %lld.", (ll )header->payload);
		return -ESOMEFAULT;
	}

	err = buffer_seek(buffer, pos);
	if (unlikely(err)) {
		fcallerror("buffer_seek", err);
		return err;
	}

	return 0;
}

