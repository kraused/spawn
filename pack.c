
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
		fcallerror("ZALLOC", err);
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
		fcallerror("ZFREE", err);
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
		/* FIXME Using pos here is a pretty bad idea since
		 *       a subsequent pack function will again cause
		 *       a reallocation.
		 */
		err = buffer_resize(self, pos);
		if (unlikely(err)) {
			fcallerror("buffer_resize", err);
			return err;
		}
	}

	self->pos  = pos;
	self->size = MAX(pos, self->size);

	return 0;
}

int buffer_resize(struct buffer *self, ll size)
{
	int err;

	if (unlikely(!self || size < 0))
		return -EINVAL;

	if (self->pos > size)
		return -EINVAL;

	if (unlikely(size > self->memsize)) {
		err = _buffer_realloc(self, size);
		if (unlikely(err)) {
			fcallerror("_buffer_realloc", err);
			return err;
		}
	}

	self->size = size;

	return 0;
}

int buffer_write(struct buffer *self, int fd)
{
	int err;
	ll bytes;

	err = do_write(fd, ((char *)self->buf) + self->pos,
	               self->size - self->pos, &bytes);
	if (unlikely(err))
		return err;

	self->pos += bytes;

	return 0;
}

int buffer_read(struct buffer *self, int fd)
{
	int err;
	ll bytes;

	err = do_read(fd, ((char *)self->buf) + self->pos,
	              self->size - self->pos, &bytes);
	if (unlikely(err))
		return err;

	self->pos += bytes;

	return 0;
}

int buffer_copy(struct buffer *self, struct buffer* other)
{
	int err;

	err = buffer_clear(self);
	if (unlikely(err)) {
		fcallerror("buffer_clear", err);
		return err;
	}

	err = buffer_resize(self, other->size);
	if (unlikely(err)) {
		fcallerror("buffer_resize", err);
		return err;
	}

	memcpy(self->buf, other->buf, other->size);
	self->size = other->size;
	self->pos  = other->pos;

	return 0;
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
			fcallerror("_buffer_realloc()", err);		\
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
	if (unlikely(self->pos + num*sizeof(T) > self->size)){		\
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

int buffer_pack_string(struct buffer *self, const char *str)
{
	int err;
	ui64 len;

	len = strlen(str) + 1;

	err = buffer_pack_ui64(self, &len, 1);
	if (unlikely(err))
		return err;

	err = buffer_pack_si8(self, (si8 *)str, len + 1);
	if (unlikely(err))
		return err;

	return 0;
}

int buffer_unpack_string(struct buffer *self, struct alloc *alloc, char **str)
{
	int err, tmp;
	ui64 len;

	err = buffer_unpack_ui64(self, &len, 1);
	if (unlikely(err))
		return err;

	err = ZALLOC(alloc, (void **)str, len, sizeof(char), "string");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail;
	}

	err = buffer_unpack_si8(self, (si8* )*str, len + 1);
	if (unlikely(err))
		goto fail;

	return 0;

fail:
	tmp = ZFREE(alloc, (void **)str, len, sizeof(char), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

int buffer_pack_array_of_str(struct buffer *self, int n, char *const *str)
{
	int err;
	ui64 len = n;
	int i;

	err = buffer_pack_ui64(self, &len, 1);
	if (unlikely(err))
		return err;

	for (i = 0; i < n; ++i) {
		err = buffer_pack_string(self, str[i]);
		if (unlikely(err))
			return err;
	}

	return 0;
}

int buffer_unpack_array_of_str(struct buffer *self, struct alloc *alloc,
                               int *n, char ***str)
{
	int err, tmp;
	ui64 len;
	int i;

	err = buffer_unpack_ui64(self, &len, 1);
	if (unlikely(err))
		return err;

	*n = len;

	err = ZALLOC(alloc, (void **)str, len, sizeof(char *), "");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail;
	}

	for (i = 0; i < len; ++i) {
		err = buffer_unpack_string(self, alloc, &(*str)[i]);
		if (unlikely(err))
			goto fail;
	}

	return 0;

fail:
	for (i = 0; i < len; ++i) {
		if (unlikely(!(*str)[i]))
			continue;

		tmp = ZFREE(alloc, (void **)&(*str)[i],
		            strlen((*str)[i]) + 1,
		            sizeof(char), "");
		if (unlikely(tmp))
			fcallerror("ZFREE", tmp);
	}

	tmp = ZFREE(alloc, (void **)str, len, sizeof(char *), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

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

	self->alloc = alloc;

	if (unlikely(0 == size)) {
		error("Size must be non-zero. Setting size = 1.");
		size = 1;
	}

	/* TODO Make the 1K buffer size configurable. */
	err = _enqueue_bunch_of_buffers(self, size, 1024);
	if (unlikely(err))
		goto fail2;	/* _enqueue_bunch_of_buffers()
				 * reports reason. */

	return 0;

fail2:
	assert(err);

	tmp = queue_dtor(&self->queue);
	if (unlikely(tmp))
		fcallerror("queue_dtor", tmp);

fail1:
	assert(err);

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
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

int buffer_pool_push(struct buffer_pool *self, struct buffer *buffer)
{
	int err, tmp;

	if (unlikely(!self || !buffer))
		return -EINVAL;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

	err = queue_enqueue(&self->queue, buffer);
	if (unlikely(err)) {
		/* Hitting the capacity should not be an issue since we only
		 * allow buffers to be pushed that have been pulled before.
		 */
		fcallerror("queue_enqueue", err);
		goto fail;
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
	assert(err);

	tmp = lock_release(&self->lock);
	if (unlikely(tmp))
		error("Failed to release lock (error %d).", tmp);

	return err;
}

int buffer_pool_pull(struct buffer_pool *self, struct buffer **buffer)
{
	int err, tmp;
	ll size;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("Failed to acquire lock (error %d).", err);
		return err;
	}

dequeue:
	err = queue_dequeue(&self->queue, (void **)buffer);
	if (unlikely(err)) {
		if (unlikely(-ENOENT != err)) {
			fcallerror("queue_dequeue", err);
			goto fail;
		}

		/* Safe to ignore error. */
		queue_size(&self->queue, &size);

		if (unlikely(0 == size)) {
			error("Queue size equals zero.");
			die();
		}

		/* TODO We could probably do better than doubling the size. */
		err = queue_change_capacity(&self->queue, 2*size);
		if (unlikely(err)) {
			fcallerror("queue_change_capacity", err);
			goto fail;
		}

		/* FIXME 1K buffer size should be configurable. */
		err = _enqueue_bunch_of_buffers(self, size, 1024);
		if (unlikely(err))
			goto fail;	/* _enqueue_bunch_of_buffers()
					 * reports reason. */

		log("Increased buffer pool size from %d to %d.", size, 2*size);

		goto dequeue;	/* try again. */
	}

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


static int _buffer_realloc(struct buffer *self, ll nmemsize)
{
	int err;

	if (unlikely(!self || (nmemsize < self->size)))
		return -EINVAL;

	err = ZREALLOC(self->alloc, (void **)&self->buf,
	               self->memsize, sizeof(char),
	               nmemsize, sizeof(char), "buffer space");
	if (unlikely(err)) {
		fcallerror("ZREALLOC", err);
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
		             sizeof(struct buffer), "buffer");
		if (unlikely(err)) {
			fcallerror("MALLOC", err);
			return err;
		}

		err = buffer_ctor(buffer, self->alloc, memsize);
		if (unlikely(err)) {
			error("struct buffer constructor failed with error %d.", err);
			goto fail1;
		}

		err = queue_enqueue(&self->queue, buffer);
		if (unlikely(err)) {
			fcallerror("queue_enqueue", err);
			goto fail2;
		}
	}

	return 0;

fail2:
	assert(err);

	tmp = buffer_dtor(buffer);
	if (unlikely(tmp))
		error("struct buffer destructor failed with error %d.", tmp);

fail1:
	assert(err);

	tmp = ZFREE(self->alloc, (void **)&buffer, 1,
	            sizeof(buffer), "buffer");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _empty_whole_queue(struct buffer_pool *self)
{
	int err;
	struct buffer *buffer;

	while (1) {
		err = queue_dequeue(&self->queue, (void **)&buffer);
		if (-ENOENT == err)
			break;
		if (unlikely(err)) {
			fcallerror("queue_dequeue", err);
			return err;
		}

		if (unlikely(!buffer)) {
			error("buffer is NULL.");
			return -ESOMEFAULT;
		}

		err = buffer_dtor(buffer);
		if (unlikely(err)) {
			error("struct buffer destructor failed with error %d.", err);
			return err;
		}

		err = ZFREE(self->alloc, (void **)&buffer, 1,
		            sizeof(buffer), "buffer");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	return 0;
}

