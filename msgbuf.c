
#define _GNU_SOURCE

#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "msgbuf.h"
#include "helper.h"


int msgbuf_ctor(struct msgbuf *self, struct alloc *alloc, si64 size)
{
	int err;

	self->alloc = alloc;
	self->size  = size;

/*
	err = ZALLOC(self->alloc, (void **)&self->buf,
	             self->size, sizeof(si8), "msgbuf");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}
*/

	list_ctor(&self->lines);

	err = lock_ctor(&self->lock);
	if (unlikely(err)) {
		fcallerror("lock_ctor", err);
		goto fail;
	}

	return 0;

fail:
/*
	tmp = ZFREE(self->alloc, (void **)&self->buf,
	            self->size, sizeof(si8), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);
*/

	return err;
}

int msgbuf_dtor(struct msgbuf *self)
{
	int err;

	err = lock_dtor(&self->lock);
	if (unlikely(err)) {
		fcallerror("lock_dtor", err);
		return err;	/* Memory leak */
	}

	/* FIXME Here we have a potential memory leak but that is really
	 *       not a problem since the implementation based on the linked
	 *       list is just temporary.
	 */

/*
	if (self->alloc) {
		err = ZFREE(self->alloc, (void **)&self->buf,
		            self->size, sizeof(si8), "");
		return err;
	}
*/

	return 0;
}

int msgbuf_print(struct msgbuf *self, const char *str)
{
	int err, tmp;
	struct msgbuf_line *line;

	err = msgbuf_lock(self);
	if (unlikely(err)) {
		fcallerror("msgbuf_lock", err);
		return err;
	}

	err = ZALLOC(self->alloc, (void **)&line,
	             sizeof(struct msgbuf_line), 1, "");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail;
	}

	list_ctor(&line->list);

	*((char *)mempcpy(line->string, str, MIN(511, strlen(str)))) = 0;

	list_insert_before(&self->lines, &line->list);

	err = msgbuf_unlock(self);
	if (unlikely(err)) {
		fcallerror("msgbuf_unlock", err);
		return err;
	}

	return 0;

fail:
	tmp = msgbuf_unlock(self);
	if (unlikely(tmp))
		fcallerror("msgbuf_unlock", tmp);

	return err;
}

