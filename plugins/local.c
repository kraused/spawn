
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"


static int _exec(struct plugin_exec *self,
                 const char *host,
                 char *const *argv);

static struct plugin_exec_ops _local_ops = {
	.exec = _exec
};

static struct plugin_exec _local = {
	.base = {
		.name = "local",
		.version = 1,
		.type = PLUGIN_EXEC
	},
	.ops = &_local_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_local;
}


static int _exec(struct plugin_exec *self,
                 const char *host,
                 char *const *argv)
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

	/* FIXME Handle EINTR? */
	p = waitpid(child, &status, 0);
	if (unlikely(p != child)) {
		error("waitpid() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	if (unlikely(!WIFEXITED(status) || 0 != WEXITSTATUS(status))) {
		error("Child did not execute properly");
		return -WEXITSTATUS(status);
	}

	return 0;
}

