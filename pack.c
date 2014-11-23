
#include <stdlib.h>
#include <string.h>


#include "config.h"
#include "compiler.h"
#include "alloc.h"
#include "error.h"
#include "helper.h"
#include "pack.h"


static int _buffer_realloc(struct buffer *self, ll nmemsize);


int buffer_ctor(struct buffer *self, struct alloc *alloc, ll memsize)
{
	int err;

	if (unlikely(!self || !alloc))
		return -EINVAL;

	memset(self, 0, sizeof(*self));

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
	if (unlikely(!self || (pos < 0) || (pos > self->size)))
		return -EINVAL;

	self->pos = pos;

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

