
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "alloc.h"
#include "error.h"
#include "helper.h"
#include "pack.h"


static int _buffer_realloc(struct buffer *self, ll nmemsize);
static int _enqueue_bunch_of_buffers(struct buffer_pool *self, ll n, ll memsize);
static int _empty_whole_queue(struct buffer_pool *self);


int buffer_ctor(struct buffer *self, struct alloc *alloc, ll memsize)
{
	int err;

	if (unlikely(!self || !alloc))
		return -EINVAL;

	memset(self, 0, sizeof(*self));

	self->alloc   = alloc;

	self->memsize = memsize;
	self->size    = 0;
	self->pos     = 0;

	err = ZALLOC(self->alloc, (void **)&self->buf,
	             self->memsize, sizeof(char), "buffer space");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	return 0;
}

int buffer_dtor(struct buffer *self)
{
	int err;

	if (unlikely(!self))
		return -EINVAL;

	err = ZFREE(self->alloc, (void **)&self->buf,
	            self->memsize, sizeof(char), "");
	if (unlikely(err)) {
		error("ZFREE() failed with error %d.", err);
		return err;
	}

	return 0;
}

int buffer_clear(struct buffer *self)
{
	if (unlikely(!self))
		return -EINVAL;

	self->size = 0;
	self->pos  = 0;

	return 0;
}

int buffer_seek(struct buffer *self, ll pos)
{
	int err;

	if (unlikely(!self || (pos < 0)))
		return -EINVAL;

	if (unlikely(pos > self->memsize)) {
		err = _buffer_realloc(self, 2*self->memsize);
		if (unlikely(err)) {
			error("_buffer_realloc() failed with"
			      " error %d.", err);
			return err;
		}
	}

	self->pos  = pos;
	self->size = MAX(pos, self->size);

	return 0;
}

ll buffer_size(struct buffer *self)
{
	if (unlikely(!self))
		return -EINVAL;

	return self->size;
}

/* TODO Better strategies than just doubling the sizes exist.
 */
#define DEFINE_PACK_UNPACK_FUNCTIONS(T)					\
int buffer_pack_ ## T(struct buffer *self, const T *value, ll num)	\
{									\
	int err;							\
									\
	if (unlikely(!self || !value || (num < 0))) 			\
		return -EINVAL;						\
									\
	if (unlikely(self->pos + num*sizeof(T) > self->memsize)) {	\
		err = _buffer_realloc(self, 2*self->memsize);		\
		if (unlikely(err)) {					\
			error("_buffer_realloc() failed with"		\
			      " error %d.", err);			\
			return err;					\
		}							\
	}								\
									\
	memcpy(self->buf + self->pos, value, num*sizeof(T));		\
	self->pos  += num*sizeof(T);					\
	self->size = MAX(self->size, self->pos);			\
									\
	return 0;							\
}									\
									\
int buffer_unpack_ ## T(struct buffer *self, T *value, ll num)		\
{									\
	if (unlikely(!self || !value || (num < 0))) 			\
		return -EINVAL;						\
									\
	if (unlikely(self->pos + num*sizeof(T) > self->memsize)) {	\
		error("Reached end of buffer.");			\
		return -ESOMEFAULT;					\
	}								\
									\
	memcpy(value, self->buf + self->pos, num*sizeof(T));		\
	self->pos  += num*sizeof(T);					\
									\
	return 0;							\
}									\

DEFINE_PACK_UNPACK_FUNCTIONS(si8)
DEFINE_PACK_UNPACK_FUNCTIONS(ui8)
DEFINE_PACK_UNPACK_FUNCTIONS(si16)
DEFINE_PACK_UNPACK_FUNCTIONS(ui16)
DEFINE_PACK_UNPACK_FUNCTIONS(si32)
DEFINE_PACK_UNPACK_FUNCTIONS(ui32)
DEFINE_PACK_UNPACK_FUNCTIONS(si64)
DEFINE_PACK_UNPACK_FUNCTIONS(ui64)

int buffer_pool_ctor(struct buffer_pool *self, struct alloc *alloc, ll size)
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
		goto fail1;
	}

	/* TODO Make the 1K buffer size configurable. */
	err = _enqueue_bunch_of_buffers(self, size, 1024);
	if (unlikely(err))
		goto fail2;	/* _enqueue_bunch_of_buffers()
				 * reports reason. */

fail2:
	tmp = queue_dtor(&self->queue);
	if (unlikely(tmp))
		error("queue_dtor() failed with error %d.", tmp);

fail1:
	tmp = lock_dtor(&self->lock);
	if (unlikely(tmp))
		error("Failed to destruct lock (error %d).", tmp);

	return err;
}

int buffer_pool_dtor(struct buffer_pool *self)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = _empty_whole_queue(self);
	if (unlikely(err))
		goto fail;	/* _empty_whole_queue() reports reason. */

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

int buffer_pool_push(struct buffer_pool *self, struct buffer *buffer)
{
	int err, tmp;
	ll size;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_enqueue(&self->queue, buffer);
	if (unlikely(err)) {
		/* Safe to ignore error. */
		queue_size(&self->queue, &size);

		/* TODO We could probably do better than doubling the size. */
		err = queue_change_capacity(&self->queue, 2*size);
		if (unlikely(err)) {
			error("queue_change_capacity() failed with error %d.", err);
			goto fail;
		}

		/* FIXME 1K buffer size should be configurable. */
		err = _enqueue_bunch_of_buffers(self, size, 1024);
		if (unlikely(err))
			goto fail;	/* _enqueue_bunch_of_buffers()
					 * reports reason. */
	}

	/* Duplication is unfortunately necessary for proper error
	 * reporting (we always return the first error in the function).
	 */
	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

int buffer_pool_pull(struct buffer_pool *self, struct buffer **buffer)
{
	int err, tmp;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_dequeue(&self->queue, (void **)buffer);
	if (unlikely(err)) {
		error("queue_dequeue() failed with error %d.", err);
		goto fail;
	}

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("Failed to release lock (error %d).", err);
		return err;
	}

	return 0;

fail:
	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}


static int _buffer_realloc(struct buffer *self, ll nmemsize)
{
	int err;

	if (unlikely(!self || (nmemsize < self->size)))
		return -EINVAL;

	err = ZREALLOC(self->alloc, (void **)&self->buf,
	               self->memsize, sizeof(char),
	               nmemsize, sizeof(char), "buffer space");
	if (unlikely(err)) {
		error("ZREALLOC() failed with error %d.", err);
		return err;
	}

	self->memsize = nmemsize;

	return 0;
}

/*
 * If the function fails it may still be that the buffer pool has been
 * extended. The constructor will clean up those buffers that have been
 * properly
 */
static int _enqueue_bunch_of_buffers(struct buffer_pool *self, ll n, ll memsize)
{
	int err, tmp;
	ll i;
	struct buffer *buffer;

	/* Lock is already held. */

	for (i = 0; i < n; ++i) {
		err = MALLOC(self->alloc, (void **)&buffer, 1,
		             sizeof(buffer), "buffer");
		if (unlikely(err)) {
			error("MALLOC() failed with error %d.", err);
			return err;
		}

		err = buffer_ctor(buffer, self->alloc, memsize);
		if (unlikely(err)) {
			error("struct buffer constructor failed with error %d.", err);
			goto fail1;
		}

		err = queue_enqueue(&self->queue, buffer);
		if (unlikely(err)) {
			error("queue_enqueue() failed with error %d.", err);
			goto fail2;
		}
	}

	return 0;

fail2:
	tmp = buffer_dtor(buffer);
	if (unlikely(tmp))
		error("struct buffer destructor failed with error %d.", tmp);

fail1:
	tmp = ZFREE(self->alloc, (void **)&buffer, 1,
	            sizeof(buffer), "buffer");
	if (unlikely(tmp))
		error("ZFREE() failed with error %d.", tmp);

	return err;
}

static int _empty_whole_queue(struct buffer_pool *self)
{
	int err;
	ll size;
	struct buffer *buffer;

	while (1) {
		queue_size(&self->queue, &size);
		if (0 == size)
			break;

		err = queue_dequeue(&self->queue, (void **)&buffer);
		if (unlikely(err)) {
			error("queue_dequeue() failed with error %d.", err);
			return err;
		}

		err = buffer_dtor(buffer);
		if (unlikely(err)) {
			error("struct buffer destructor failed with error %d.", err);
			return err;
		}

		err = ZFREE(self->alloc, (void **)&buffer, 1,
		            sizeof(buffer), "buffer");
		if (unlikely(err)) {
			error("ZFREE() failed with error %d.", err);
			return err;
		}
	}

	return 0;
}

