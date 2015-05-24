
#ifndef SPAWN_TASK_H_INCLUDED
#define SPAWN_TASK_H_INCLUDED 1

#include "list.h"
#include "thread.h"
#include "queue.h"
#include "protocol.h"	/* For message_user */


/*
 * A user task. The logic of a task is implemented in a dynamically
 * loaded plugin and runs in a separate thread.
 */
struct task
{
	struct alloc		*alloc;

	struct spawn		*spawn;

	struct task_plugin	*plu;
	struct thread		thread;

	int			argc;
	char 			**argv;

	/* Communication channel allocated for this task.
	 */
	int			channel;

	/* Queue for received messages.
	 */
	struct queue_with_lock	recvq;
};

/*
 * Record of a received message
 */
struct task_recvd_message
{
	int			src;
	struct message_user	msg;
};

/*
 * Constructor for struct task. path should be the path to the DSO file.
 */
int task_ctor(struct task *self, struct alloc *alloc,
              struct spawn *spawn, const char *path,
              int argc, char **argv, int channel);
int task_dtor(struct task *self);

/*
 * Execute the main() function of the task in
 */
int task_start(struct task *self);

/*
 * Cancel the task.
 */
int task_cancel(struct task *self);

/*
 * Check if the task finished by itself.
 */
static inline int task_is_done(struct task *self)
{
	return thread_is_done(&self->thread);
}

/*
 * Join the thread running the task main routine.
 */
static inline int task_thread_join(struct task *self)
{
	return thread_join(&self->thread);
}

/*
 * Exit code of the task thread. Can be called after task_thread_join().
 */
static inline int task_exit_code(struct task *self)
{
	return self->thread.err;
}

/*
 * Enqueue a message for the task.
 */
int task_enqueue_message(struct task *self, struct task_recvd_message *msg);

/*
 * Write a line to stdout or stderr from a task plugin.
 */
int task_plugin_api_write_line_stdout(struct task_plugin *plu, const char *line);
int task_plugin_api_write_line_stderr(struct task_plugin *plu, const char *line);

/*
 * Send a message to another task. The task identifier equals the process identifier
 * in the tree.
 */
int task_plugin_api_send(struct task_plugin *plu, int dst, ui8 *bytes, ui64 len);

/*
 * Receive a message.
 */
int task_plugin_api_recv(struct task_plugin *plu, struct task_recvd_message **msg);

#endif

