
#ifndef SPAWN_PACK_H_INCLUDED
#define SPAWN_PACK_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "queue.h"

/*
 * TODO Handle endianess. I do not like the idea to convert everything from
 *      host to (IP) network order since we mostly work on little endian systems.
 */

/*
 * A (resizable) buffer used, e.g., for message transfer.
 */
struct buffer
{
	struct alloc	*alloc;

	/* Size of the allocated memory for the buffer. */
	ll		memsize;
	char		*buf;

	/* Size of the buffer, i.e., number of bytes written to
	 * the memory region. */
	ll		size;
	/* Position pointer used for packing. */
	ll		pos;
};

/*
 * Buffer constructor. memsize is the initial memory size and should be > 0.
 */
int buffer_ctor(struct buffer *self, struct alloc *alloc, ll memsize);

/*
 * Buffer destructor.
 */
int buffer_dtor(struct buffer *self);

/*
 * Clear the buffer. This will not release the memory.
 */
int buffer_clear(struct buffer *self);

/*
 * Change the position pointer in the buffer. This is useful for example in order to
 * write the header (which does contain the payload size) after having filled the buffer
 * with the payload.
 */
int buffer_seek(struct buffer *self, ll pos);

/*
 * Resize the buffer. It is an error if size is smaller than the current position value.
 */
int buffer_resize(struct buffer *self, ll size);

/*
 * Query the buffer size. Note that this is not equal to the amount of memory allocated
 * for the struct buffer.
 */
static inline ll buffer_size(struct buffer *self)
{
	return self->size;
}

/*
 * Check if the buffer position and size coincide.
 */
static inline int buffer_pos_equal_size(struct buffer *self)
{
	return (self->pos == self->size);
}

/*
 * Write a buffer to fd. Advance the position pointer by the
 * number of bytes written.
 */
int buffer_write(struct buffer *self, int fd);

/*
 * Read buffer content from fd. Advance the position pointer by the
 * number of bytes written.
 */
int buffer_read(struct buffer *self, int fd);

/*
 * Copy content from other to self.
 */
int buffer_copy(struct buffer *self, struct buffer* other);

/*
 * Pack and unpack values. Note that the number of elements is not stored in the buffer. If
 * necessary it should be explicitly written to the buffer prior to writing the actual data.
 *
 * Both, pack and unpack functions, advance the position pointer.
 */
int buffer_pack_si8(struct buffer *self, const si8 *value, ll num);
int buffer_unpack_si8(struct buffer *self, si8 *value, ll num);
int buffer_pack_ui8(struct buffer *self, const ui8 *value, ll num);
int buffer_unpack_ui8(struct buffer *self, ui8 *value, ll num);
int buffer_pack_si16(struct buffer *self, const si16 *value, ll num);
int buffer_unpack_si16(struct buffer *self, si16 *value, ll num);
int buffer_pack_ui16(struct buffer *self, const ui16 *value, ll num);
int buffer_unpack_ui16(struct buffer *self, ui16 *value, ll num);
int buffer_pack_si32(struct buffer *self, const si32 *value, ll num);
int buffer_unpack_si32(struct buffer *self, si32 *value, ll num);
int buffer_pack_ui32(struct buffer *self, const ui32 *value, ll num);
int buffer_unpack_ui32(struct buffer *self, ui32 *value, ll num);
int buffer_pack_si64(struct buffer *self, const si64 *value, ll num);
int buffer_unpack_si64(struct buffer *self, si64 *value, ll num);
int buffer_pack_ui64(struct buffer *self, const ui64 *value, ll num);
int buffer_unpack_ui64(struct buffer *self, ui64 *value, ll num);

/*
 * A thread-safe pool of buffers. Due to the asynchronous messaging scheme used in this
 * application keeping track of all buffers is a tricky business in particular if we like
 * to reuse buffers. The buffer pool (partially) solves the problem. A disadvantage of
 * this scheme is that it will not scale to many concurrent threads.
 */
struct buffer_pool
{
	struct alloc	*alloc;
	struct queue	queue;
	struct lock	lock;
};

/*
 * Create a new buffer pool. This function may only be called by a single function.
 */
int buffer_pool_ctor(struct buffer_pool *self, struct alloc *alloc, ll size);

/*
 * Destructor. Make sure no one is using the buffer before calling this function
 * on one thread.
 */
int buffer_pool_dtor(struct buffer_pool *self);

/*
 * Enqueue a buffer into the buffer. You should only push() buffers that
 * have been previously pull()ed from the same pool.
 */
int buffer_pool_push(struct buffer_pool *self, struct buffer *buffer);

/*
 * Obtain a buffer from the pool. If the pool is empty it will be resized and
 * new buffers will be allocated.
 */
int buffer_pool_pull(struct buffer_pool *self, struct buffer **buffer);

#endif

