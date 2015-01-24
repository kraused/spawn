
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"


static int _exec(struct exec_plugin *self,
                 const char *host,
                 char *const *argv);
static char **_combine_argv(char *const *oargv, const char *host);
static char **_prepare_env();

static const char *_slurm_argv[] = {
	"/usr/bin/srun",
	"-w",	/* Host is following */
	/* Not null terminated - Will be merged with
	 * user provided arguments.
	 */
};

static struct exec_plugin_ops _slurm_ops = {
	.exec = _exec
};

static struct exec_plugin _slurm = {
	.base = {
		.name = "slurm",
		.version = 1,
		.type = PLUGIN_EXEC
	},
	.ops = &_slurm_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_slurm;
}


static int _exec(struct exec_plugin *self,
                 const char *host,
                 char *const *argv)
{
	long long child;
	long long p;
	int status;
	char **env;

	if (unlikely(!host))
		return -EINVAL;

	child = fork();
	if (0 == child) {
		argv = _combine_argv(argv, host);
		env  = _prepare_env();

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
		error("Child neithr terminated nor was terminated by a signal.");
		return -ESOMEFAULT;
	}

	return 0;
}

static char **_combine_argv(char *const *oargv, const char *host)
{
	int i;
	char **argv;

	i = 0;
	while (oargv[i]) i++;
	++i;

	argv = malloc((i + 1 + sizeof(_slurm_argv)/sizeof(_slurm_argv[0]))*sizeof(char *));
	if (unlikely(!argv)) {
		error("malloc() returned. NULL");
		return NULL;
	}

	/* We can freely strdup() in here since _combine_argv() is called by
	 * the forked process which will next execute an execve() call.
	 */

	i = 0;
	for (i = 0; i < sizeof(_slurm_argv)/sizeof(_slurm_argv[0]); ++i)
		argv[i] = strdup(_slurm_argv[i]);

	argv[i] = strdup(host);
	++i;

	while (*oargv) {
		argv[i] = *oargv;
		++i;
		++oargv;
	}
	argv[i] = NULL;

	return argv;
}

static char **_prepare_env()
{
	char **env;
	char *jobid;

	/* FIXME This is a bit of hack. It would be better to retrieve the job id from
	 *       the options rather than the environment.
	 */

	jobid = getenv("SLURM_JOB_ID");
	if (unlikely(!jobid)) {
		error("SLURM_JOB_ID is not set.");
		return NULL;
	}

	env = malloc(2*sizeof(char**));
	if (unlikely(!env)) {
		error("malloc() returned. NULL");
		return NULL;
	}

	env[1] = NULL;
	env[0] = malloc(13 + strlen(jobid) + 1);
	if (unlikely(!env[0])) {
		error("malloc() returned. NULL");
		return NULL;
	}
	snprintf(env[0], 13 + strlen(jobid) + 1, "SLURM_JOB_ID=%s", jobid);

	return env;
}

