
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "task.h"
#include "helper.h"
#include "plugin.h"
#include "spawn.h"
#include "protocol.h"


static int _thread_main(void *arg);
static int _send_write_message(struct spawn *spawn, int type, const char *line);


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

	/* Required for the task_plugin_api_X functions.
	 */
	self->plu->spawn = spawn;

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

int task_plugin_api_write_line_stdout(struct task_plugin *plu, const char *line)
{
	return _send_write_message(plu->spawn, MESSAGE_TYPE_WRITE_STDOUT, line);
}

int task_plugin_api_write_line_stderr(struct task_plugin *plu, const char *line)
{
	return _send_write_message(plu->spawn, MESSAGE_TYPE_WRITE_STDERR, line);
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

static int _send_write_message(struct spawn *spawn, int type, const char *line)
{
	int err;
	struct message_header       header;
	struct message_write_stderr msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = type;

	/* We can ignore the channel at this point since this is a standard
	 * message type supported by the loop() function.
	 */

	msg.lines = line;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

