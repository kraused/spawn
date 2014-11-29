
#include "config.h"
#include "compiler.h"
#include "error.h"
#include "thread.h"


static void *_thread_main(void *args);


int thread_ctor(struct thread *self)
{
	int err, tmp;

	if (unlikely(!self))
		return -EINVAL;

	err = pthread_cond_init(&self->cond, NULL);
	if (unlikely(err)) {
		error("pthread_cond_init() failed with error %d.", err);
		return err;
	}

	err = pthread_mutex_init(&self->mutex, NULL);
	if (unlikely(err)) {
		error("pthread_mutex_init() failed with error %d.", err);
		goto fail1;
	}

	err = pthread_create(&self->handle, NULL, _thread_main, self);
	if (unlikely(err)) {
		error("pthread_create() failed with error %d.", err);
		goto fail2;
	}

	return 0;

fail2:
	assert(err);

	tmp = pthread_mutex_destroy(&self->mutex);
	if (unlikely(tmp))
		error("pthread_mutex_destroy() failed with error %d.", tmp);

fail1:
	assert(err);

	tmp = pthread_cond_destroy(&self->cond);
	if (unlikely(tmp))
		error("pthread_cond_destroy() failed with error %d.", tmp);

	return err;
}

int thread_dtor(struct thread *self)
{
	int err;

	err = pthread_mutex_destroy(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_destroy() failed with error %d.", err);
		return -err;
	}

	err = pthread_cond_destroy(&self->cond);
	if (unlikely(err)) {
		error("pthread_cond_destroy() failed with error %d.", err);
		return -err;
	}

	return 0;
}

int thread_start(struct thread *self, int (*main)(void *), void *arg)
{
	int err;

	err = pthread_mutex_lock(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_lock() failed with error %d.", err);
		return -err;
	}

	self->main = main;
	self->arg  = arg;

	err = pthread_mutex_unlock(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_unlock() failed with error %d.", err);
		return err;
	}

	err = pthread_cond_signal(&self->cond);
	if (unlikely(err)) {
		error("pthread_cond_signal() failed with error %d.", err);
		return -err;
	}

	return 0;
}

int thread_join(struct thread *self)
{
	int err;
	void *p;

	err = pthread_join(self->handle, &p);
	if (unlikely(err)) {
		error("pthread_join() failed with error %d.", err);
		return -err;
	}

	if (unlikely(p != self)) {
		error("_thread_main() did not execute properly.");
		return -ESOMEFAULT;
	}

	return 0;
}

int lock_ctor(struct lock *self)
{
	int err;

	err = pthread_mutexattr_init(&self->attr);
	if (unlikely(err)) {
		error("pthread_mutexattr_init() failed with error %d.", err);
		return -err;
	}

	err = pthread_mutexattr_settype(&self->attr, PTHREAD_MUTEX_RECURSIVE);
	if (unlikely(err)) {
		error("pthread_mutexattr_settype() failed with error %d.", err);
		return -err;
	}

	err = pthread_mutex_init(&self->mutex, &self->attr);
	if (unlikely(err)) {
		error("pthread_mutex_init() failed with error %d.", err);
		return -err;
	}

	return 0;
}

int lock_dtor(struct lock *self)
{
	int err;

	err = pthread_mutex_destroy(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_destroy() failed with error %d.", err);
		return -err;
	}

	err = pthread_mutexattr_destroy(&self->attr);
	if (unlikely(err)) {
		error("pthread_mutexattr_destroy() failed with error %d.", err);
		return -err;
	}

	return 0;
}

int lock_acquire(struct lock *self)
{
	int err;

	while (1) {
		err = pthread_mutex_lock(&self->mutex);
		if (unlikely(err)) {
			error("pthread_mutex_lock() failed with error %d.", err);
			return -err;
		}

		break;
	}

	return 0;
}

int lock_release(struct lock *self)
{
	int err;

	while (1) {
		err = pthread_mutex_unlock(&self->mutex);
		if (unlikely(err)) {
			error("pthread_mutex_unlock() failed with error %d.", err);
			return err;
		}

		break;
	}

	return 0;
}


static void *_thread_main(void *arg)
{
	int err;
	struct thread *self = (struct thread *)arg;

	if (unlikely(!self)) {
		error("self pointer is NULL.");
		pthread_exit(NULL);
	}

	err = pthread_mutex_lock(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_lock() failed with error %d.", err);
		pthread_exit(NULL);
	}

	while (!self->main) {
		err = pthread_cond_wait(&self->cond, &self->mutex);
		if (unlikely(err)) {
			error("pthread_cond_wait() failed with error %d.", err);
			pthread_exit(NULL);
		}
	}

	err = pthread_mutex_unlock(&self->mutex);
	if (unlikely(err)) {
		error("pthread_mutex_unlock() failed with error %d.", err);
		pthread_exit(NULL);
	}

	self->err = self->main(self->arg);

	pthread_exit((void *)self);
}

