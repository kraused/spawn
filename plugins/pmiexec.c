
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"
#include "helper.h"
#include "task.h"
#include "spawn.h"
#include "alloc.h"

#include "pmi/server.h"

static int _local(struct task_plugin *self,
                  int argc, char **argv);
static int _other(struct task_plugin *self,
                  int argc, char **argv);
static int _watch_child(struct task_plugin *self, long long *child, int *status,
                        int fdo, int fde, struct pmi_server *pmisrv);
static int _read_from_child(int fd, char *line, int *len, ll *size,
                            struct task_plugin *plu,
                            int (*flush)(struct task_plugin *, const char *));
static int _kvs_fence(struct pmi_server *srv, void *ctx);

static struct task_plugin_ops _pmiexec_ops = {
	.local = _local,
	.other = _other
};

static struct task_plugin _pmiexec = {
	.base = {
		.name = "pmiexec",
		.version = 1,
		.type = PLUGIN_TASK
	},
	.ops = &_pmiexec_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_pmiexec;
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
	int fdpmi[2];
	struct pmi_server pmisrv;

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

	/* Socket for PMI communication. We cannot use a pipe here
         * since server and client read from and write to the same
         * file descriptor.
	 */
	err = socketpair(AF_UNIX, SOCK_STREAM, 0, fdpmi);
	if (unlikely(err < 0)) {
		error("pipe() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	child = fork();
	if (0 == child) {
		char pmifd[16];
		char *env[] = {pmifd, NULL};

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

		close(fdpmi[0]);
		snprintf(pmifd, sizeof(pmifd), "PMI_FD=%d", fdpmi[1]);

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
	close(fdpmi[1]);

	err = pmi_server_ctor(&pmisrv, self->task->spawn->alloc, fdpmi[0],
	                      /* Subtract one since the root process is part of the
	                       * tree but not of the PMI job.
	                       */
	                      self->task->spawn->tree.here - 1,
	                      self->task->spawn->tree.size - 1,
	                      _kvs_fence, (void *)self);
	if (unlikely(err)) {
		fcallerror("pmi_server_ctor", err);
		return err;
	}

	err = _watch_child(self, &child, &status, fdo[0], fde[0], &pmisrv);
	if (unlikely(err)) {
		fcallerror("_watch_child", err);
		return err;
	}

	close(fdo[0]);
	close(fde[0]);
	close(fdpmi[0]);

	err = pmi_server_dtor(&pmisrv);
	if (unlikely(err))
		fcallerror("pmi_server_dtor", err);

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
                        int fdo, int fde, struct pmi_server *pmisrv)
{
	int err;
	struct pollfd pollfds[3];
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
		pollfds[2].fd = pmisrv->fd;
		pollfds[0].events = POLLIN;
		pollfds[1].events = POLLIN;
		pollfds[2].events = POLLIN;

		/* TODO Optimize the timeout.
		 */

		err = do_poll(pollfds, 3, 1, &n);
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
		if ((pollfds[2].revents & POLLIN) && !(pollfds[2].revents & POLLHUP)) {
			err = pmi_server_talk(pmisrv);
			if (unlikely(err))
				fcallerror("pmi_server_talk", err);
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

static int _kvs_fence(struct pmi_server *srv, void *ctx)
{
	struct task_plugin *self = (struct task_plugin *)ctx;
	int err;
	struct task_recvd_message *msg;
	int i, k;
	ui8 *bytes;
	ui64 len;

	/* TODO Use the tree structure to speed this up and reduce the
	 *      number of messages.
	 *
	 *      Unfortunately that is not that easy. The root of the tree is
	 *      process zero which does not spawn an (PMI) process and does
	 *      not receive a "kvs-fence" request as the others do. Thus, in
	 *      order to use the tree the root process would need to continuously
	 *      spin and call task_plugin_api_recv() which would require a high
	 *      CPU load.
	 */

	if (1 == self->task->spawn->tree.here) {
		for (i = 2; i < self->task->spawn->tree.size; ++i) {
			k = 0;
			do {
				err = task_plugin_api_recv(self, &msg);
				++k;
			} while (err);	/* TODO Can we do better than spinning here?
			                 */

			err = pmi_server_kvs_unpack(srv, msg->msg.bytes, msg->msg.len);
			if (unlikely(err))
				fcallerror("pmi_server_kvs_unpack", err);

			/* FIXME We cannot use free_message_payload() here because
			 *       we do not have the header. However, we cannot be sure
			 *       the byte array was allocated with srv->alloc.
			 */
			err = ZFREE(srv->alloc, (void **)&msg->msg.bytes, msg->msg.len, sizeof(ui8), "");
			if (unlikely(err))
				fcallerror("ZFREE", err);
		}

		err = pmi_server_kvs_pack(srv, srv->alloc, &bytes, &len);
		if (unlikely(err))
			fcallerror("pmi_server_kvs_pack", err);

		for (i = 2; i < self->task->spawn->tree.size; ++i) {
			err = task_plugin_api_send(self, i, bytes, len);
			if (unlikely(err))
				fcallerror("task_plugin_api_send", err);
		}

		err = ZFREE(srv->alloc, (void **)&bytes, len, sizeof(ui8), "");
		if (unlikely(err))
			fcallerror("ZFREE", err);
	} else {
		pmi_server_kvs_pack(srv, srv->alloc, &bytes, &len);

		err = pmi_server_kvs_free(srv);
		if (unlikely(err))
			fcallerror("pmi_server_kvs_free", err);

		err = task_plugin_api_send(self, 1, bytes, len);
		if (unlikely(err))
			fcallerror("task_plugin_api_send", err);

		err = ZFREE(srv->alloc, (void **)&bytes, len, sizeof(ui8), "");
		if (unlikely(err))
			fcallerror("ZFREE", err);

		k = 0;
		do {
			err = task_plugin_api_recv(self, &msg);
			++k;
		} while (err);	/* TODO Can we do better than spinning here?
		                 */

		pmi_server_kvs_unpack(srv, msg->msg.bytes, msg->msg.len);
		if (unlikely(err))
			fcallerror("pmi_server_kvs_unpack", err);

		/* FIXME We cannot use free_message_payload() here because
		 *       we do not have the header. However, we cannot be sure
		 *       the byte array was allocated with srv->alloc.
		 */
		err = ZFREE(srv->alloc, (void **)&msg->msg.bytes, msg->msg.len, sizeof(ui8), "");
		if (unlikely(err))
			fcallerror("ZFREE", err);
	}

	return 0;
}

