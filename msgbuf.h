
#ifndef SPAWN_MSGBUF_H_INCLUDED
#define SPAWN_MSGBUF_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "list.h"


struct msgbuf_line;

/*
 * Buffer for output lines. Currently implemented as a linked list of
 * strings.
 * FIXME A compressed (ring) buffer should be used in the future.
 */
struct msgbuf
{
	struct alloc	*alloc;
	struct lock	lock;

	si64		size;	/* Currently unused.
			         */

	struct list	lines;
};

#undef  MSGBUF_MAX_LINE_LENGTH
#define MSGBUF_MAX_LINE_LENGTH 512

/*
 * One line in the message buffer.
 */
struct msgbuf_line
{
	char		string[MSGBUF_MAX_LINE_LENGTH];

	struct list	list;
};

/*
 * Create and destroy a message buffer.
 */
int msgbuf_ctor(struct msgbuf *self, struct alloc *alloc, si64 size);
int msgbuf_dtor(struct msgbuf *self);

/*
 * Lock the message buffer. Any concurrent call to msgbuf_printf() will block.
 */
static inline int msgbuf_lock(struct msgbuf *self)
{
	return lock_acquire(&self->lock);
}

/*
 * Unlock the message buffer.
 */
static inline int msgbuf_unlock(struct msgbuf *self)
{
	return lock_release(&self->lock);
}

/*
 * Append a string to the buffer.
 */
int msgbuf_print(struct msgbuf *self, const char *str);

#endif

