
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "ints.h"
#include "thread.h"
#include "atomic.h"
#include "watchdog.h"


struct _watchdog
{
	struct thread	thread;
	int		timeout;
	ll		last;
} _watchdog;

static int _watchdog_thread(void *args);
static void _sleep(struct _watchdog *self);
static void _bite() __attribute__((noreturn));


int let_the_watchog_loose(int timeout)
{
	int err;

	err = thread_ctor(&_watchdog.thread);
	if (unlikely(err)) {
		error("Failed to create watchdog thread.");
		return err;
	}

	_watchdog.timeout = timeout;
	_watchdog.last    = llnow();

	err = thread_start(&_watchdog.thread, _watchdog_thread, &_watchdog);
	if (unlikely(err)) {
		error("Failed to start watchdog.");
		/* TODO Call thread_dtor()? Would that result in a deadlock? */
		return err;
	}

	return 0;
}

int calm_the_watchdog()
{
	ll t = llnow();
	atomic_write(_watchdog.last, t);

	return 0;
}


static int _watchdog_thread(void *args)
{
	struct _watchdog *self = (struct _watchdog *)args;
	ll t1, t2;

	log("Watchdog is loose.");

	while (1) {
		_sleep(self);

		t1 = llnow();
		t2 = atomic_read(self->last);

		if (unlikely((t1 - t2) > self->timeout)) {
			warn("Watchdog is going to bite: Killing processes.");
			_bite();
		}
	}
}

static void _sleep(struct _watchdog *self)
{
	struct timespec ts;

	ts.tv_sec  = self->timeout + 1;
	ts.tv_nsec = 0;

	nanosleep(&ts, NULL);
}

static void _bite()
{
	struct timespec ts;

	ts.tv_sec  = 0;
	ts.tv_nsec = 1000000;

	while (1) {
		kill(0, SIGTERM);
		nanosleep(&ts, NULL);
		kill(0, SIGKILL);
	}
}

