
#ifndef SPAWN_THREAD_H_INCLUDED
#define SPAWN_THREAD_H_INCLUDED 1

#include <pthread.h>
#include "atomic.h"

/*
 * Possible states during the life of a thread.
 */
enum
{
	THREAD_STATE_INVAL	= 0,
	THREAD_STATE_INITED,	/* Thread is alive but waiting for work
				 * (see thread_start()). */
	THREAD_STATE_STARTED,
	THREAD_STATE_DONE,
	THREAD_STATE_JOINED
};

/*
 * A thread handle.
 */
struct thread
{
	pthread_t		handle;

	pthread_cond_t		cond;
	pthread_mutex_t		mutex;

	int			(*main)(void *);
	void			*arg;

	/* Return value of the thread. Do not access before
	 * thread_join() has been called. */
	int			err;

	/* Thread state. Kept up-to-date by the thread and
	 * read-only for others. */
	int			state;
};

/*
 * Constructor for struct thread. When this function returns the
 * new thread is running but will not perform anything until
 * thread_start() is called.
 */
int thread_ctor(struct thread *self);

/*
 * Destructor for struct thread.
 */
int thread_dtor(struct thread *self);

/*
 * Start the thread execution. The
 */
int thread_start(struct thread *self, int (*main)(void *), void *arg);

/*
 * Join the thread. When thread_join() returns the err member of the thread
 * structure can be accessed.
 */
int thread_join(struct thread *self);

/*
 * Check if a thread is done processing its task (but has not been joined yet).
 * If thread_is_done() returns true thread_join() will not block.
 */
static inline int thread_is_done(struct thread *self)
{
	return (THREAD_STATE_DONE == atomic_read(self->state));
}

/*
 * A reentrant lock
 */
struct lock
{
	pthread_mutexattr_t	attr;
	pthread_mutex_t		mutex;
};

/*
 * Constructor for struct lock.
 */
int lock_ctor(struct lock *self);

/*
 * Destructor.
 */
int lock_dtor(struct lock *self);

/*
 * Acquire the lock.
 */
int lock_acquire(struct lock *self);

/*
 * Release the lock.
 */
int lock_release(struct lock *self);


/*
 * A condition variable. Condition variables can be used to wait for the
 * occurence of certain conditions (as signaled by a concurrent thread) without
 * need for spinning and the corresponding lock congestion issues.
 */
struct cond_var
{
	pthread_cond_t		cond;
	pthread_mutex_t		mutex;
};

/*
 * Constructor for struct cond_var.
 */
int cond_var_ctor(struct cond_var *self);

/*
 * Destructor for struct cond_var.
 */
int cond_var_dtor(struct cond_var *self);

/*
 * Acquire the mutex associated with the condition variable.
 */
int cond_var_lock_acquire(struct cond_var *self);

/*
 * Release the mutex associated with the condition variable.
 */
int cond_var_lock_release(struct cond_var *self);

/*
 * Block on the condition variable.
 */
int cond_var_wait(struct cond_var *self);

/*
 * Block on the condition variable with a timeout value. The function
 * returns -ETIMEDOUT if the function timed out.
 */
int cond_var_timedwait(struct cond_var *self, struct timespec *abstime);

/*
 * Unblock one thread.
 */
int cond_var_signal(struct cond_var *self);

/*
 * Unblock all threads.
 */
int cond_var_broadcast(struct cond_var *self);

#endif

