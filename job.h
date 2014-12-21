
#ifndef SPAWN_JOB_H_INCLUDED
#define SPAWN_JOB_H_INCLUDED 1

#include "list.h"

struct spawn;


/*
 * Job types
 */
enum
{
	JOB_TYPE_NOOP		= 0,
	JOB_TYPE_BUILD_TREE,
	JOB_TYPE_JOIN
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
 * Task for building a tree.
 */
struct job_build_tree
{
	struct job	job;
};

/*
 */
struct job_join
{
	struct job	job;

	int		father;
};

/*
 * Allocate a struct job_build_tree on the heap and call the constructor.
 */
int alloc_job_build_tree(struct alloc *alloc, struct job **self);

/*
 * Allocate a struct job_join on the heap and call the constructor.
 */
int alloc_job_join(struct alloc *alloc, int father, struct job **self);

/*
 * Destroy and free a heap allocated job structure.
 */
int free_job(struct job **self);

#endif
