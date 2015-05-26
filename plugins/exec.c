
#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"
#include "helper.h"
#include "task.h"

static int _local(struct task_plugin *self,
                  int argc, char **argv);
static int _other(struct task_plugin *self,
                  int argc, char **argv);
static int _watch_child(struct task_plugin *self, long long *child, int *status,
                        int fdo, int fde);
static int _read_from_child(int fd, char *line, int *len, ll *size,
                            struct task_plugin *plu,
                            int (*flush)(struct task_plugin *, const char *));

static struct task_plugin_ops _exec_ops = {
	.local = _local,
	.other = _other
};

static struct task_plugin _exec = {
	.base = {
		.name = "exec",
		.version = 1,
		.type = PLUGIN_TASK
	},
	.ops = &_exec_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_exec;
}

static int _local(struct task_plugin *self,
                  int argc, char **argv)
{
	return 0;
}

static int _other(struct task_plugin *self,
                  int argc, char **argv)
{
	int err;
	long long child;
	int status;
	int fdo[2], fde[2];

	err = pipe(fdo);
	if (unlikely(err < 0)) {
		error("pipe() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	err = pipe(fde);
	if (unlikely(err < 0)) {
		error("pipe() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	child = fork();
	if (0 == child) {
		char *env[] = {NULL};

		close(fdo[0]);
		if (fdo[1] != STDOUT_FILENO) {
			dup2 (fdo[1], STDOUT_FILENO);
			close(fdo[1]);
		}

		close(fde[0]);
		if (fde[1] != STDERR_FILENO) {
			dup2 (fde[1], STDERR_FILENO);
			close(fde[1]);
		}

		execve(argv[0], argv, env);
		error("execve() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		exit(-1);
	}
	else if (unlikely(-1 == child)) {
		error("fork() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	log("Child process %d is alive.", (int )child);

	/* FIXME Error handling.
	 */
	close(fdo[1]);
	close(fde[1]);

	err = _watch_child(self, &child, &status, fdo[0], fde[0]);

	close(fdo[0]);
	close(fde[0]);

	if (unlikely(err)) {
		fcallerror("_watch_child", err);
		return err;
	}

	if (likely(WIFEXITED(status))) {
		log("Child process terminated with exit code %d.", WEXITSTATUS(status));
		return -WEXITSTATUS(status);
	} else if(WIFSIGNALED(status)) {
		log("Child was terminated by signal %d.", WTERMSIG(status));
		return -ESOMEFAULT;
	} else {
		error("Child neither terminated nor was terminated by a signal.");
		return -ESOMEFAULT;
	}

	return 0;
}

#undef  MAX_LINE_LEN
#define MAX_LINE_LEN	512

static int _watch_child(struct task_plugin *self, long long *child, int *status,
                        int fdo, int fde)
{
	int err;
	struct pollfd pollfds[2];
	char lo[MAX_LINE_LEN];
	int leno;
	char le[MAX_LINE_LEN];
	int lene;
	long long p;
	int quit, k, n;
	ll size;

	leno = 0;
	lene = 0;

	do {
		memset(pollfds, 0, sizeof(pollfds));

		pollfds[0].fd = fdo;
		pollfds[1].fd = fde;
		pollfds[0].events = POLLIN | POLLPRI | POLLERR;
		pollfds[1].events = POLLIN | POLLPRI | POLLERR;

		/* TODO Optimize the timeout.
		 */

		err = do_poll(pollfds, 2, 1, &n);
		if (unlikely(err))
			return err;

		k = 0;

		if (pollfds[0].revents & POLLIN) {
			err = _read_from_child(pollfds[0].fd, lo, &leno, &size,
			                       self, task_plugin_api_write_line_stdout);
			if (unlikely(err))
				fcallerror("_read_from_child", err);

			if (0 != size)
				++k;
		}
		if (pollfds[1].revents & POLLIN) {
			err = _read_from_child(pollfds[1].fd, le, &lene, &size,
			                       self, task_plugin_api_write_line_stderr);
			if (unlikely(err))
				fcallerror("_read_from_child", err);

			if (0 != size)
				++k;
		}

		quit = (0 == *child) && (0 == k);

		if (*child) {
			p = waitpid(*child, status, WNOHANG);
			if (p) {
				if (unlikely(p != *child)) {
					error("waitpid() failed. errno = %d says '%s'.",
					      errno, strerror(errno));
					return -errno;
				}

				*child = 0;
			}
		}
	} while (!quit);

	if (leno) {
		err = task_plugin_api_write_line_stdout(self, lo);
		if (unlikely(err))
			fcallerror("task_plugin_api_write_line_stdout", err);
	}
	if (lene) {
		err = task_plugin_api_write_line_stderr(self, le);
		if (unlikely(err))
			fcallerror("task_plugin_api_write_line_stderr", err);
	}

	return 0;
}

static int _read_from_child(int fd, char *line, int *len, ll *size,
                            struct task_plugin *plu,
                            int (*flush)(struct task_plugin *, const char *))
{
	int err;
	char buf[MAX_LINE_LEN];
	const char *x;
	int n;

	err = do_read(fd, buf, MAX_LINE_LEN - (*len + 1), size);
	if (unlikely(err)) {
		fcallerror("do_read", err);
		return err;
	}
	buf[*size] = 0;

	x = strchr(buf, '\n');
	if (x) {
		*((char *)mempcpy(line + (*len), buf, (x - buf) + 1)) = 0;

		err = flush(plu, line);
		if (unlikely(err)) {
			fcallerror("flush", err);
			return err;
		}

		*len = 0;
		++x;
	} else
		x = buf;

	n = strlen(x);
	*((char *)mempcpy(line + (*len), x, n)) = 0;
	*len += n;

	if (MAX_LINE_LEN == ((*len) + 1)) {
		err = flush(plu, line);
		if (unlikely(err)) {
			fcallerror("flush", err);
			return err;
		}

		*len = 0;
	}

	return 0;
}

