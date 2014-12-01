
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
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	self->alloc    = alloc;
	self->capacity = capacity;
	self->head     = 0;
	self->tail     = 0;

	return 0;
}

int queue_dtor(struct queue *self)
{
	int err;

	err = ZFREE(self->alloc, (void **)&self->buf, self->capacity,
	            sizeof(void *), "queue");
	if (unlikely(err)) {
		error("ZFREE() failed with error %d.", err);
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
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	for (i = 0; i < self->size; ++i)
		buf[i] = self->buf[(self->head + i) % self->capacity];

	err = ZFREE(self->alloc, (void **)&self->buf, self->capacity,
	            sizeof(void *), "queue");
	if (unlikely(err)) {
		error("ZFREE() failed with error %d.", err);
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

	self->buf[self->tail] = p;

	self->tail = (self->tail + 1) % self->capacity;
	self->size += 1;

	return 0;
}

int queue_dequeue(struct queue *self, void **p)
{
	if (unlikely(0 == self->size))
		return -ENOENT;

	*p = self->buf[self->head];

	self->head  = (self->head + 1) % self->capacity;
	self->size -= 1;

	return 0;
}

int queue_peek(struct queue *self, void **p)
{
	if (unlikely(0 == self->size))
		return -ENOENT;

	*p = self->buf[self->head];

	return 0;
}

