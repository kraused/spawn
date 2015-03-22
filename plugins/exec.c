
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

/* FIXME These should not be used by plugins.
 */
#include "spawn.h"
#include "protocol.h"


static int _local(struct task_plugin *self,
                  int argc, char **argv);
static int _other(struct task_plugin *self,
                  int argc, char **argv);
static int _send_write_stdout(struct spawn *spawn, const char *str);
static int _send_write_stderr(struct spawn *spawn, const char *str);

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
	int n;
	long long child;
	long long p;
	int status;
	int fdo[2], fde[2];
	struct pollfd pollfds[2];
	char line[512];
	ll size;

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

	while (1) {
		memset(pollfds, 0, sizeof(pollfds));

		pollfds[0].fd = fdo[0];
		pollfds[1].fd = fde[1];
		pollfds[0].events = POLLIN | POLLPRI | POLLERR;
		pollfds[1].events = POLLIN | POLLPRI | POLLERR;

		err = do_poll(pollfds, 2, 1, &n);

		if (unlikely((0 == child) && (0 == n)))
			break;

		if (pollfds[0].revents & POLLIN) {
			do_read (pollfds[0].fd, line, sizeof(line) - 1, &size);
			line[size] = 0;

			/* FIXME (Line) buffering?
			 */
			_send_write_stdout(self->spawn, line);
		}
		if (pollfds[1].revents & POLLIN) {
			do_read (pollfds[1].fd, line, sizeof(line), &size);

			/* FIXME (Line) buffering?
			 */
			_send_write_stderr(self->spawn, line);
		}

		if (0 == child)
			continue;

		p = waitpid(child, &status, WNOHANG);
		if (0 == p)
			continue;
		if (unlikely(p != child)) {
			error("waitpid() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		child = 0;
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

/* FIXME Plugins should not use such low-level features.
 */

static int _send_write_stdout(struct spawn *spawn, const char *str)
{
	int err;
	struct message_header       header;
	struct message_write_stdout msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_WRITE_STDOUT;

	/* FIXME channel?
	 */

	msg.lines = str;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _send_write_stderr(struct spawn *spawn, const char *str)
{
	int err;
	struct message_header       header;
	struct message_write_stderr msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_WRITE_STDERR;

	/* FIXME channel?
	 */

	msg.lines = str;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

