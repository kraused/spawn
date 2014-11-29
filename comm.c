
#include "config.h"
#include "compiler.h"
#include "error.h"
#include "comm.h"


static int _comm_thread(void *);
static int _comm_queue_ctor(struct comm_queue *self,
                            struct alloc * alloc, ll size);
static int _comm_queue_dtor(struct comm_queue *self);


int comm_ctor(struct comm *self, struct alloc *alloc, struct network *net,
              ll sendqsz, ll recvqsz)
{
	int err;

	err = _comm_queue_ctor(&self->sendq, alloc, sendqsz);
	if (unlikely(err))
		return err;	/* _comm_queue_ctor() reported reason. */

	err = _comm_queue_ctor(&self->recvq, alloc, recvqsz);
	if (unlikely(err))
		goto fail1;	/* _comm_queue_ctor() reported reason. */

	self->stop = 0;
	self->alloc = alloc;
	self->net   = net;

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

	return 0;
}

int comm_start_processing(struct comm *self)
{
	int err;

	err = thread_start(&self->thread, _comm_thread, self);
	if (unlikely(err)) {
		error("Failed to create communication thread (error %d).", err);
		return err;
	}

	return 0;
}

int comm_stop_processing(struct comm *self)
{
	int err;

	err = thread_join(&self->thread);
	if (unlikely(err)) {
		error("Failed to join communication thread (error %d).", err);
		return err;
	}

	return 0;
}

int comm_enqueue(struct comm *self, struct buffer *buffer)
{
	return -ENOTIMPL;
}

int comm_dequeue(struct comm *self, struct buffer **buffer)
{
	return -ENOTIMPL;
}


static int _comm_thread(void *arg)
{
	struct comm *self = (struct comm *)arg;

	while (1) {
		if (0 == self->stop)
			break;

		/* FIXME Do some work here */

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

fail:
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
	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

