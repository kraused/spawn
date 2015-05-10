
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "queue.h"


int queue_ctor(struct queue *self, struct alloc *alloc, ll capacity)
{
	int err;

	if (unlikely(!self || !alloc || capacity < 1))
		return -EINVAL;

	memset(self, 0, sizeof(*self));

	err = ZALLOC(alloc, (void **)&self->buf, capacity,
	             sizeof(void *), "queue");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	self->alloc    = alloc;
	self->capacity = capacity;
	self->head     = 0;
	self->tail     = 0;
	self->size     = 0;

	return 0;
}

int queue_dtor(struct queue *self)
{
	int err;

	err = ZFREE(self->alloc, (void **)&self->buf, self->capacity,
	            sizeof(void *), "queue");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	memset(self, 0, sizeof(*self));

	return 0;
}

int queue_change_capacity(struct queue *self, ll capacity)
{
	int err;
	void **buf;
	ll i;

	if (unlikely(capacity < self->size))
		return -EINVAL;

	/* We do not use REALLOC here but rather ZALLOC() and ZFREE()
	 * since the useful data may be split into two seperate groups
	 * in the buffer and it is easier to copy the data out-of-place. */

	err = ZALLOC(self->alloc, (void **)&buf, capacity,
	             sizeof(void *), "queue");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	for (i = 0; i < self->size; ++i)
		buf[i] = self->buf[(self->head + i) % self->capacity];

	err = ZFREE(self->alloc, (void **)&self->buf, self->capacity,
	            sizeof(void *), "queue");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	self->buf      = buf;
	self->capacity = capacity;
	self->head     = 0;
	self->tail     = self->size;

	return 0;
}

int queue_enqueue(struct queue *self, void *p)
{
	if (unlikely(self->capacity == self->size))
		return -ENOMEM;

	self->buf[self->tail % self->capacity] = p;

	self->tail += 1;
	self->size += 1;

	return 0;
}

int queue_dequeue(struct queue *self, void **p)
{
	if (unlikely(0 == self->size))
		return -ENOENT;

	*p = self->buf[self->head % self->capacity];

	self->head += 1;
	self->size -= 1;

	return 0;
}

int queue_peek(struct queue *self, void **p)
{
	if (unlikely(0 == self->size))
		return -ENOENT;

	*p = self->buf[self->head % self->capacity];

	return 0;
}

int queue_with_lock_ctor(struct queue_with_lock *self,
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

int queue_with_lock_dtor(struct queue_with_lock *self)
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

int queue_with_lock_size(struct queue_with_lock *self, ll *size)
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

int queue_with_lock_enqueue(struct queue_with_lock *self, void *p)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_enqueue(&self->queue, p);
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

int queue_with_lock_dequeue(struct queue_with_lock *self, void **p)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_dequeue(&self->queue, p);
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

int queue_with_lock_peek(struct queue_with_lock *self, void **p)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_peek(&self->queue, p);
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

