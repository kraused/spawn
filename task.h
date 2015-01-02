
#ifndef SPAWN_TASK_H_INCLUDED
#define SPAWN_TASK_H_INCLUDED 1

#include "list.h"
#include "thread.h"

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

	struct list		list;

	/* Communication channel allocated for this task.
	 */
	int			channel;
};

/*
 * Constructor for struct task. path should be the path to the DSO file.
 */
int task_ctor(struct task *self, struct alloc *alloc,
              struct spawn *spawn, const char *path,
              int channel);
int task_dtor(struct task *self);

/*
 * Execute the main() function of the task in
 */
int task_start(struct task *self);

/*
 * Cancel the task.
 */
int task_cancel(struct task *self);

#endif

