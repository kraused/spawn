
#ifndef SPAWN_JOB_H_INCLUDED
#define SPAWN_JOB_H_INCLUDED 1

#include "list.h"

struct spawn;
struct task;


/*
 * Job types
 */
enum
{
	JOB_TYPE_NOOP		= 0,
	JOB_TYPE_BUILD_TREE,
	JOB_TYPE_JOIN,
	JOB_TYPE_TASK,
	JOB_TYPE_EXIT
};

/*
 * Base structure for all jobs.
 *
 * We use the term "job" instead of "task" since the latter is reserved
 * for the task done by task plugins.
 */
struct job
{
	struct alloc	*alloc;

	int		type;
	struct list	list;

			/* Make progress. If the job is completed the
			 * last argument is set to 1 and zero otherwise. */
	int		(*work)(struct job *self, struct spawn *spawn,
			        int *completed);
};

/*
 * Internal datastructure for the management of childs in
 * struct job_build_tree
 */
struct job_build_tree_child
{
	int	id;	/* Participant id */
	int	host;
	int	nhosts;
	enum {
		UNBORN,
		UNKNOWN,
		ALIVE,
		DEAD,
		READY
	}	state;
	ll	spawned;	/* Time when we requested the children
				 * to be spawned. Used to known when to
				 * declare a child as dead. */
};

/*
 * Task for building a tree.
 */
struct job_build_tree
{
	struct job			job;

	struct alloc			*alloc;

	/* Hosts are identified by their index into the spawn->hosts
	 * array.
	 */
	int				nhosts;
	int				*hosts;

	int				nchildren;
	struct job_build_tree_child	*children;

	/* Used to keep track of the progress. */
	int				phase;

	ll				start;	/* Timestamp */
};

/*
 * Task for the process of joining the network.
 */
struct job_join
{
	struct job	job;

	int		parent;
	int		acked;	/* Set to one if a RESPONSE_JOIN has been
			         * received. */
};

/*
 * Task for loading plugins.
 */
struct job_task
{
	struct job	job;

	char		*path;
	ui16		channel;
	char		**argv;

	struct task	*task;

	int		acks;	/* Number of responses received from
				 * children.
				 */

	int		phase;
};

/*
 * Task for terminating the program.
 */
struct job_exit
{
	struct job	job;

	/* In order to avoid hangs during program termination the process
	 * is forced to quit immediately if the job keeps in the list for
	 * too long.
	 */
	struct timespec	start;
	struct timespec	timeout;

	int		acks;	/* Number of responses received from
				 * children.
				 */

	int		phase;
};

/*
 * Allocate a struct job_build_tree on the heap and call the constructor.
 * If nhosts == spawn->nhosts the hosts parameter is ignored and may equal
 * NULL since the self->hosts is necessarily equal to [0 .. (nhosts-1)].
 */
int alloc_job_build_tree(struct alloc *alloc, struct spawn *spawn,
                         int nhosts, int *hosts, struct job **self);

/*
 * Allocate a struct job_join on the heap and call the constructor.
 */
int alloc_job_join(struct alloc *alloc, int parent, struct job **self);

/*
 * Allocate a struct job_task on the heap and call the constructor.
 */
int alloc_job_task(struct alloc *alloc, const char* path,
                   ui16 channel, struct job **self);

/*
 * Allocate a struct job_exit on the heap and call the constructor.
 */
int alloc_job_exit(struct alloc *alloc, const struct timespec *timeout,
                   struct job **self);

/*
 * Destroy and free a heap allocated job structure.
 */
int free_job(struct job **self);

#endif

