
#ifndef SPAWN_THREAD_H_INCLUDED
#define SPAWN_THREAD_H_INCLUDED 1


#include <pthread.h>

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

#endif

