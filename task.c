
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "task.h"
#include "helper.h"
#include "plugin.h"
#include "spawn.h"


static int _thread_main(void *arg);


int task_ctor(struct task *self, struct alloc *alloc,
              struct spawn *spawn, const char *path,
              int argc, char **argv, int channel)
{
	int err;
	struct plugin *plu;

	self->alloc   = alloc;
	self->spawn   = spawn;
	self->channel = channel;

	plu = load_plugin(path);
	if (unlikely(!plu))
		return -ESOMEFAULT;

	self->plu = cast_to_task_plugin(plu);
	if (unlikely(!self->plu)) {
		error("Plugin '%s' is not an task plugin.", path);
		return -EINVAL;
	}

	self->plu->spawn = spawn;	/* FIXME */

	err = thread_ctor(&self->thread);
	if (unlikely(err)) {
		fcallerror("thread_ctor", err);
		return err;
	}

	self->argc = argc;

	err = array_of_str_dup(self->alloc, argc + 1, argv, &self->argv);
	if (unlikely(err)) {
		fcallerror("array_of_str_dup", err);
		return err;
	}

	return 0;
}

int task_dtor(struct task *self)
{
	int err;

	err = thread_dtor(&self->thread);
	if (unlikely(err)) {
		fcallerror("thread_dtor", err);
		return err;
	}

	err = array_of_str_free(self->alloc, self->argc + 1, &self->argv);
	if (unlikely(err)) {
		fcallerror("array_of_str_free", err);
		return err;
	}

	return 0;
}

int task_start(struct task *self)
{
	int err;

	err = thread_start(&self->thread, _thread_main, self);
	if (unlikely(err)) {
		fcallerror("thread_start", err);
		return err;
	}

	return 0;
}

int task_cancel(struct task *self)
{
	int err;

	err = thread_cancel(&self->thread);
	if (unlikely(err)) {
		fcallerror("thread_cancel", err);
		return err;
	}

	return 0;
}

static int _thread_main(void *arg)
{
	struct task *self = (struct task *)arg;
	int err;

	if (0 == self->spawn->tree.here) {
		err = self->plu->ops->local(self->plu, self->argc, self->argv);
	} else {
		err = self->plu->ops->other(self->plu, self->argc, self->argv);
	}

	return err;
}

