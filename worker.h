
#ifndef SPAWN_WORKER_H_INCLUDED
#define SPAWN_WORKER_H_INCLUDED 1

#include "thread.h"
#include "queue.h"

struct exec_plugin;


/*
 * Work item.
 */
struct exec_work_item
{
	char	*host;
	int	argc;
	char	**argv;
	int	client;	/* Id of requesting host. */
};

/*
 * Worker pool used to spawn executable in parallel.
 */
struct exec_worker_pool
{
	struct alloc		*alloc;

	int			nthreads;
	struct thread		*threads;

	/* Queue of exec_work_item pointers.
	 */
	struct queue		queue;
	struct cond_var		cond;

	/* Flag used to indicate to threads to terminate.
	 */
	int			done;

	struct exec_plugin	*exec;
};

int exec_worker_pool_ctor(struct exec_worker_pool *self, struct alloc *alloc,
                          int nthreads, ll capacity, struct exec_plugin *exec);
int exec_worker_pool_dtor(struct exec_worker_pool *self);

/*
 * Start the threads.
 */
int exec_worker_pool_start(struct exec_worker_pool *self);

/*
 * Stop all threads. After this action it is not possible to restart
 * them again.
 */
int exec_worker_pool_stop(struct exec_worker_pool *self);

/*
 * Enqueue a new work item. The function returns -ENOMEM if the queue
 * is full.
 */
int exec_worker_pool_enqueue(struct exec_worker_pool *self,
                             struct exec_work_item *wkitem);

#endif

