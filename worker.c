
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "worker.h"
#include "helper.h"
#include "plugin.h"


static int _thread_main(void *arg);
static int _work_available(struct exec_worker_pool *self);
static int _all_threads_done(struct exec_worker_pool *self);
static int _do_exec_work(struct exec_worker_pool *self,
                         struct exec_work_item *wkitem);
static int _free_exec_work_item(struct exec_worker_pool *self,
                                struct exec_work_item **wkitem);

int exec_worker_pool_ctor(struct exec_worker_pool *self, struct alloc *alloc,
                          int nthreads, ll capacity, struct exec_plugin *exec)
{
	int err, tmp;
	int i, k;

	self->alloc    = alloc;
	self->nthreads = nthreads;
	self->done     = 0;
	self->exec     = exec;

	err = queue_ctor(&self->queue, alloc, capacity);
	if (unlikely(err)) {
		fcallerror("queue_ctor", err);
		return err;
	}

	err = cond_var_ctor(&self->cond);
	if (unlikely(err)) {
		fcallerror("cond_var_ctor", err);
		goto fail1;
	}

	err = ZALLOC(alloc, (void **)&self->threads, nthreads,
	             sizeof(struct thread), "threads");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail2;
	}

	k = nthreads;

	for (i = 0; i < nthreads; ++i) {
		err = thread_ctor(&self->threads[i]);
		if (unlikely(err)) {
			fcallerror("thread_ctor", err);
			k = i;
			goto fail3;
		}
	}

	/* FIXME Variable timeout value. The timeout value
	 *       determines how long it takes to stop the
	 *       threads.
	 */
	self->timeout.tv_sec  = 0;
	self->timeout.tv_nsec = 1000L*1000L;	/* Millisecond */

	return 0;

fail3:
	for (i = 0; i < k; ++i) {
		tmp = thread_dtor(&self->threads[i]);
		if (unlikely(tmp))
			fcallerror("thread_dtor", tmp);
	}

	tmp = ZFREE(alloc, (void **)&self->threads, nthreads,
	            sizeof(struct thread), "threads");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

fail2:
	tmp = cond_var_dtor(&self->cond);
	if (unlikely(tmp))
		fcallerror("cond_var_dtor", tmp);

fail1:
	tmp = queue_dtor(&self->queue);
	if (unlikely(tmp))
		fcallerror("queue_dtor", tmp);

	return err;
}

int exec_worker_pool_dtor(struct exec_worker_pool *self)
{
	int err;
	int i;

	if (!self->done) {
		error("Thread pool is still running.");
		return -ESOMEFAULT;
	}

	for (i = 0; i < self->nthreads; ++i) {
		err = thread_dtor(&self->threads[i]);
		if (unlikely(err)) {
			fcallerror("thread_dtor", err);
			return err;
		}
	}

	err = ZFREE(self->alloc, (void **)&self->threads,
	            self->nthreads, sizeof(struct thread), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	err = cond_var_dtor(&self->cond);
	if (unlikely(err)) {
		fcallerror("cond_var_dtor", err);
		return err;
	}

	err = queue_dtor(&self->queue);
	if (unlikely(err)) {
		fcallerror("queue_dtor", err);
		return err;
	}

	return 0;
}

int exec_worker_pool_start(struct exec_worker_pool *self)
{
	int err;
	int i;

	for (i = 0; i < self->nthreads; ++i) {
		err = thread_start(&self->threads[i], _thread_main, self);
		if (unlikely(err)) {
			fcallerror("thread_start", err);
			die();	/* Not sure how to properly fix that
				 * situation.
				 */
		}
	}

	return 0;
}

int exec_worker_pool_stop(struct exec_worker_pool *self)
{
	atomic_write(self->done, 1);
	/* A busy loop should be fine in this situation. We anyway only stop
	 * the worker pool if the queues are empty and in that case the threads
	 * will not utilize the CPU at all so we have some free cycles to spare.
	 */
	while (!_all_threads_done(self));

	return 0;
}

int exec_worker_pool_enqueue(struct exec_worker_pool *self,
                             struct exec_work_item *wkitem)
{
	int err, tmp;

	err = cond_var_lock_acquire(&self->cond);
	if (unlikely(err)) {
		fcallerror("cond_var_lock_acquire", err);
		die();
	}

	err = queue_enqueue(&self->queue, wkitem);
	if (-ENOMEM == err)
		goto fail;	/* Expected failure. Do not call error(). */
	if (unlikely(err)) {
		fcallerror("queue_enqueue", err);
		goto fail;
	}

	err = cond_var_signal(&self->cond);
	if (unlikely(err)) {
		fcallerror("lock_var_broadcast", err);
		goto fail;
	}

	err = cond_var_lock_release(&self->cond);
	if (unlikely(err)) {
		fcallerror("cond_var_lock_release", err);
		die();
	}

	return 0;

fail:
	tmp = cond_var_lock_release(&self->cond);
	if (unlikely(tmp)) {
		fcallerror("cond_var_lock_release", tmp);
		die();
	}

	return err;
}


static int _thread_main(void *arg)
{
	struct exec_worker_pool *self = (struct exec_worker_pool *)arg;
	int err;
	struct exec_work_item *wkitem;
	struct timespec abstime;

	while (1) {
		if (atomic_read(self->done))
			break;

		err = cond_var_lock_acquire(&self->cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_acquire", err);
			die();
		}

		err = abstime_near_future(&self->timeout, &abstime);
		if (unlikely(err))
			fcallerror("abstime_near_future", err);

		while (!_work_available(self)) {
			err = cond_var_timedwait(&self->cond, &abstime);
			if (-ETIMEDOUT == err)
				break;
			if (unlikely(err)) {
				fcallerror("cond_var_timedwait", err);
				die();
			}
		}

		wkitem = NULL;

		err = queue_dequeue(&self->queue, (void **)&wkitem);
		if (unlikely(err && (-ENOENT != err))) {
			fcallerror("queue_dequeue", err);
			die();
		}

		err = cond_var_lock_release(&self->cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_release", err);
			die();
		}

		if (wkitem) {
			_do_exec_work(self, wkitem);
		}
	}

	return 0;
}

static int _work_available(struct exec_worker_pool *self)
{
	ll size;

	queue_size(&self->queue, &size);

	return (size > 0);
}

static int _all_threads_done(struct exec_worker_pool *self)
{
	int i, n;

	n = 0;
	for (i = 0; i < self->nthreads; ++i)
		n += thread_is_done(&self->threads[i]);

	return (self->nthreads ==n );
}

static int _do_exec_work(struct exec_worker_pool *self,
                         struct exec_work_item *wkitem)
{
	int err, tmp;

	debug("Spawning process '%s' on host '%s' on request from %d.",
	      wkitem->argv[0], wkitem->host, wkitem->client);

	err = self->exec->ops->exec(self->exec, wkitem->host, wkitem->argv);
	if (unlikely(err)) {
		error("Spawn plugin exec function failed with error %d.", err);
		goto fail;
	}

	err = _free_exec_work_item(self, &wkitem);
	if (unlikely(err)) {
		fcallerror("_free_exec_work_item", err);
		return err;
	}

	return 0;

fail:
	tmp = _free_exec_work_item(self, &wkitem);
	if (unlikely(tmp))
		fcallerror("_free_exec_work_item", tmp);

	return err;
}

static int _free_exec_work_item(struct exec_worker_pool *self,
                                struct exec_work_item **wkitem)
{
	int err;

	err = array_of_str_free(self->alloc,
	                        ((*wkitem)->argc + 1),
	                        &(*wkitem)->argv);
	if (unlikely(err))
		return err;

	err = ZFREE(self->alloc, (void **)wkitem, 1,
	            sizeof(struct exec_work_item), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

