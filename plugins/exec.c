
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"


static int _local(struct task_plugin *self,
                  int argc, char **argv);
static int _other(struct task_plugin *self,
                  int argc, char **argv);

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
	long long child;
	long long p;
	int status;

	child = fork();
	if (0 == child) {
		char *env[] = {NULL};

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

	/* TODO Handle EINTR? */
	p = waitpid(child, &status, 0);
	if (unlikely(p != child)) {
		error("waitpid() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
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

