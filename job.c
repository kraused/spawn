
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "job.h"
#include "alloc.h"
#include "comm.h"
#include "spawn.h"
#include "protocol.h"
#include "helper.h"
#include "task.h"


static int _job_build_tree_ctor(struct job_build_tree *self, struct alloc* alloc,
                                struct spawn *spawn, int nhosts, int *hosts);
static int _job_build_tree_dtor(struct job_build_tree *self);
static int _free_job_build_tree(struct alloc *alloc, struct job_build_tree **self);
static int _build_tree_work(struct job *job, struct spawn *spawn, int *completed);
static int _build_tree_listen(struct job_build_tree *self, struct spawn *spawn);
static int _open_listenfds(struct job_build_tree *self, struct spawn *spawn,
                           int fds[NETWORK_MAX_LISTENFDS], int *nfds);
static int _create_socket_and_bind(struct sockaddr *addr, ull addrlen, int *fd);
static int _listen_listenfds(struct job_build_tree *self, struct spawn *spawn,
                             int *fds, int nfds);
static int _build_tree_spawn_children(struct job_build_tree *self, struct spawn *spawn);
static int _send_request_build_tree_message(struct job_build_tree *self, struct spawn *spawn,
                                            int dest, int nhosts, si32 *hosts);
static int _send_response_build_tree_message(struct job_build_tree *self, struct spawn *spawn);
static int _job_task_ctor(struct job_task *self, struct alloc *alloc,
                          const char *path, int argc, char **argv,
                          ui16 channel);
static int _job_task_dtor(struct job_task *self);
static int _free_job_task(struct alloc *alloc, struct job_task **self);
static int _task_work(struct job *job, struct spawn *spawn, int *completed);
static int _task_send_request(struct spawn *spawn, const char *path,
                              int argc, char **argv,
                              ui16 channel);
static int _task_send_response(struct spawn *spawn, int ret);
static int _job_exit_ctor(struct job_exit *self, struct alloc *alloc,
                          const struct timespec *timeout);
static int _job_exit_dtor(struct job_exit *self);
static int _free_job_exit(struct alloc *alloc, struct job_exit **self);
static int _exit_work(struct job *job, struct spawn *spawn, int *completed);
static int _exit_send_request(struct spawn *spawn);
static int _exit_send_response(struct spawn *spawn);
static int _prepare_task_job(struct spawn *spawn);


int alloc_job_build_tree(struct alloc *alloc, struct spawn *spawn,
                         int nhosts, int *hosts, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_build_tree), "struct job_build_tree");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_build_tree_ctor((struct job_build_tree *)*self, alloc,
	                             spawn, nhosts, hosts);
}

int alloc_job_task(struct alloc *alloc, const char* path,
                   int argc, char **argv,
                   ui16 channel, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_task), "struct job_task");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_task_ctor((struct job_task *)*self, alloc, path, argc, argv, channel);
}

int alloc_job_exit(struct alloc *alloc, const struct timespec *timeout,
                   struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_exit), "struct job_exit");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_exit_ctor((struct job_exit *)*self, alloc, timeout);
}

int free_job(struct job **self)
{
	int err;

	switch ((*self)->type) {
	case JOB_TYPE_BUILD_TREE:
		err = _free_job_build_tree((*self)->alloc,
		                           (struct job_build_tree **)self);
		break;
	case JOB_TYPE_TASK:
		err = _free_job_task((*self)->alloc,
		                     (struct job_task **)self);
		break;
	case JOB_TYPE_EXIT:
		err = _free_job_exit((*self)->alloc,
		                     (struct job_exit **)self);
		break;
	default:
		error("Unknown job type %d.", (*self)->type);
		err = -ESOMEFAULT;
	}

	if (unlikely(err))
		return err;	/* Error reporting is done in the _free_...(struct alloc *alloc, )
				 * functions.
				 */

	return 0;
}


static int _job_build_tree_ctor(struct job_build_tree *self, struct alloc* alloc,
                                struct spawn *spawn, int nhosts, int *hosts)
{
	int err, tmp;
	int i, quot;
	int treewidth;

	err = optpool_find_by_key_as_int(spawn->opts, "TreeWidth", &treewidth);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		die();	/* Very unlikely. */
	}

	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_BUILD_TREE;
	self->job.work  = _build_tree_work;

	list_ctor(&self->job.list);

	self->alloc  = alloc;
	self->phase  = 1;
	self->nhosts = nhosts;

	err = ZALLOC(alloc, (void **)&self->hosts,
	             self->nhosts, sizeof(int), "hosts");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	if ((spawn->nhosts == nhosts)) {
		for (i = 0; i < self->nhosts; ++i)
			self->hosts[i] = i;
	} else {
		memcpy(self->hosts, hosts, self->nhosts*sizeof(int));
	}

	self->nchildren = MIN(treewidth, self->nhosts);
	quot = self->nhosts/self->nchildren;

	log("# children = %d", self->nchildren);

	err = ZALLOC(alloc, (void **)&self->children, self->nchildren,
	             sizeof(struct job_build_tree_child), "children");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail;
	}

	for (i = 0; i < self->nchildren; ++i) {
		self->children[i].host    = quot*i;
		self->children[i].nhosts  = quot - 1;
		self->children[i].id      = spawn->tree.here + 1 + quot*i;
		self->children[i].state   = UNBORN;
		self->children[i].spawned = 0;
	}

	self->children[self->nchildren-1].nhosts =
		self->nhosts - (self->children[self->nchildren-1].host + 1);

	return 0;

fail:
	tmp = ZFREE(alloc, (void **)&self->hosts, self->nhosts, sizeof(int), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _job_build_tree_dtor(struct job_build_tree *self)
{
	int err;

	err = ZFREE(self->alloc, (void **)&self->children, self->nchildren,
	            sizeof(struct job_build_tree_child), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	err = ZFREE(self->alloc, (void **)&self->hosts,
	            self->nhosts, sizeof(int), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _free_job_build_tree(struct alloc *alloc, struct job_build_tree **self)
{
	int err;

	err = _job_build_tree_dtor(*self);
	if (unlikely(err))
		fcallerror("_job_build_tree_dtor", err);

	err = ZFREE(alloc, (void **)self, 1,
	            sizeof(struct job_build_tree), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _build_tree_work(struct job *job, struct spawn *spawn, int *completed)
{
	struct job_build_tree *self = (struct job_build_tree *)job;
	int err;

	/* Phase 1: Spawn children.
	 */
	if (1 == self->phase) {
		self->start = llnow();

		err = _build_tree_listen(self, spawn);
		if (unlikely(err)) {
			fcallerror("_build_tree_listen", err);
			die();	/* FIXME */
		}

		if (0 == spawn->tree.here) {
			err = exec_worker_pool_start(spawn->wkpool);
			if (unlikely(err)) {
				fcallerror("exec_worker_pool_start", err);
				die();	/* Better to stop at this point
					 * since we cannot be sure that the
					 * code does not deadlock later on.
					 */
			}
		}

		err = _build_tree_spawn_children(self, spawn);
		if (unlikely(err)) {
			fcallerror("_build_tree_spawn_children", err);
			die();	/* FIXME */
		}

		self->phase += 1;
	}

	/* Phase 2: Wait for children to connect back and send out
	 *          BUILD_TREE commands.
	 */
	if (2 == self->phase) {
		int i, k;

		k = 0;
		for (i = 0; i < self->nchildren; ++i) {
			if ((UNBORN  == self->children[i].state) ||
			    (UNKNOWN == self->children[i].state)) {
				/* FIXME Variable timeout value
				 */
				if (unlikely((llnow() - self->children[i].spawned) > 60)) {
					error("Child %d did not connect back.", i);
					die(); /* FIXME */
				}
			}

			/* FIXME Include DEAD children */
			k += (ALIVE == self->children[i].state);
		}

		if (k == self->nchildren) {
			log("All children are alive after %lld second(s).", llnow() - self->start);

			for (i = 0; i < self->nchildren; ++i) {
				if (0 == self->children[i].nhosts) {
					self->children[i].state = READY;
					continue;
				}

				err = _send_request_build_tree_message(self, spawn,
				                                       self->children[i].id,
				                                       self->children[i].nhosts,
				                                       self->hosts + self->children[i].host + 1);
				if (unlikely(err)) {
					fcallerror("_send_request_build_tree_message", err);
					die();	/* FIXME */
				}
			}

			self->phase = 3;
		}
	}

	/* Phase 3: Wait for all children to report completion
	 */
	if (3 == self->phase) {
		int i, k;

		k = 0;
		for (i = 0; i < self->nchildren; ++i) {
			k += (READY == self->children[i].state);
		}

		if (k == self->nchildren) {
			err = _send_response_build_tree_message(self, spawn);
			if (unlikely(err)) {
				fcallerror("_send_response_build_tree_message", err);
				die();		/* FIXME */
			}

			*completed = 1;
		}
	}

	if (*completed) {
		if (0 == spawn->tree.here) {
			err = exec_worker_pool_stop(spawn->wkpool);
			if (unlikely(err))
				fcallerror("exec_worker_pool_stop", err);
				/* Still possible to go on.
				 */

			err = _prepare_task_job(spawn);
			if (unlikely(err))
				fcallerror("_prepare_task_job", err);
				/* loop() will terminate.
				 */
		}

		network_debug_print_lft(&spawn->tree);

		log("Finished building the tree after %lld second(s).", llnow() - self->start);
	}

	return 0;
}

static int _build_tree_listen(struct job_build_tree *self, struct spawn *spawn)
{
	int err;
	int fds[NETWORK_MAX_LISTENFDS];
	int n;

	n = 0;
	err = _open_listenfds(self, spawn, fds, &n);
	if (unlikely(err))
		fcallerror("_open_listenfds", err);

	err = _listen_listenfds(self, spawn, fds, n);
	if (unlikely(err))
		fcallerror("_listen_listenfds", err);

	/* Temporarily disable the communication thread. Otherwise it
	 * happens that we have to wait for seconds before acquiring the
	 * lock.
	 */
	err = comm_stop_processing(&spawn->comm);
	if (unlikely(err))
		error("Failed to temporarily stop the communication thread.");

	err = network_lock_acquire(&spawn->tree);
	if (unlikely(err))
		die();

	err = network_add_listenfds(&spawn->tree, fds, n);
	if (unlikely(err))
		fcallerror("network_add_listenfd", err);

	err = network_lock_release(&spawn->tree);
	if (unlikely(err))
		die();

	err = comm_resume_processing(&spawn->comm);
	if (unlikely(err)) {
		error("Failed to resume the communication thread.");
		die();	/* Pretty much impossible that this happen. If it does
			 * we are screwed though. */
	}


	return 0;
}

static int _open_listenfds(struct job_build_tree *self, struct spawn *spawn,
                           int fds[NETWORK_MAX_LISTENFDS], int *nfds)
{
	int err;
	int i, j, k;
	const char *host;
	struct ipv4interface *ifaces[NETWORK_MAX_LISTENFDS];
	struct ipv4interface *iface;
	ui32 ip, port;

	memset(ifaces, 0, sizeof(ifaces));

	k = 0;
	for (i = 0; i < self->nchildren; ++i) {
		host = spawn->hosts[self->hosts[self->children[i].host]];

		err = map_hostname_to_interface(&spawn->hostinfo, host, &iface);
		if (unlikely(err)) {
			fcallerror("map_hostname_to_interface", err);
			continue;
		}

		for (j = 0; j < k; ++j) {
			if (ifaces[j] == iface)
				break;
		}

		if (k == j) {
			if (unlikely(k == NETWORK_MAX_LISTENFDS)) {
				error("NETWORK_MAX_LISTENFDS is too low.");
				die();
			}

			err = _create_socket_and_bind((struct sockaddr *)&iface->addr,
			                              sizeof(iface->addr), &fds[k]);
			if (unlikely(err)) {
				fcallerror("bind_socket_to_interface", err);
				continue;
			}

			ifaces[k++] = iface;
		}

		err = sockaddr(fds[j], &ip,
		                       &port);
		if (unlikely(err))
			fcallerror("sockaddr", err);

		self->children[i].conn.sin_family      = AF_INET;
		self->children[i].conn.sin_port        = port;
		self->children[i].conn.sin_addr.s_addr = htonl(ip);
	}

	*nfds = k;

	return 0;
}

static int _create_socket_and_bind(struct sockaddr *addr, ull addrlen, int *fd)
{
	int err;

	*fd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(fd < 0)) {
		error("socket() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	err = bind(*fd, addr, addrlen);
	if (unlikely(err)) {
		error("bind() failed. errno = %d says '%s'.", errno, strerror(errno));
		close(*fd);
		return -errno;
	}

	return 0;
}

static int _listen_listenfds(struct job_build_tree *self, struct spawn *spawn,
                             int *fds, int nfds)
{
	int err;
	int i;
	int backlog;

	err = optpool_find_by_key_as_int(spawn->opts, "TreeSockBacklog", &backlog);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		backlog = 128;
	}

	for (i = 0; i < nfds; ++i) {
		err = listen(fds[i], backlog);
		if (unlikely(err))
			error("listen() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
	}

	return 0;
}

static int _build_tree_spawn_children(struct job_build_tree *self, struct spawn *spawn)
{
	int err;
	int i, n;
	struct message_header       header;
	struct message_request_exec msg;
	char host[HOST_NAME_MAX];
	char argv1[32];
	char argv2[32];
	char argv3[32];
	char argv4[32];
	char argv5[32];
	char *argv[] = {SPAWN_EXE_OTHER,
	                argv1, argv2,
	                argv3, argv4,
	                argv5, NULL};

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;
	header.dst   = 0;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_REQUEST_EXEC;

	msg.host = host;
	msg.argv = argv;

	/* TODO It is really a waste of bandwidth to send a single message per spawn request.
	 *	We should rather extend the MESSAGE_TYPE_EXEC to support spawning multiple
	 *      processes at once.
	 */

	for (i = 0; i < self->nchildren; ++i) {
		/* FIXME Capture errors from those snprintf()s! */

		n = snprintf(host, sizeof(host), "%s",
		            spawn->hosts[self->hosts[self->children[i].host]]);
		if (unlikely(n == sizeof(host))) {
			error("Hostname truncated");
			continue;
		}

		snprintf(argv1, sizeof(argv1), "%s", inet_ntoa(self->children[i].conn.sin_addr));
		snprintf(argv2, sizeof(argv2), "%d", (int )self->children[i].conn.sin_port);

		snprintf(argv3, sizeof(argv3), "%d", spawn->tree.here);	/* my participant id */
		snprintf(argv4, sizeof(argv4), "%d", spawn->nhosts);	/* number of hosts */
		snprintf(argv5, sizeof(argv5), "%d", self->children[i].id);

		debug("'%s' '%s' '%s' '%s' '%s'", argv1, argv2, argv3, argv4, argv5);

		err = spawn_send_message(spawn, &header, (void *)&msg);
		if (unlikely(err))
			fcallerror("spawn_send_message", err);	/* Continue anyway. Node will be marked
								 * as down later when we do not hear back
								 */

		self->children[i].spawned = llnow();
		self->children[i].state   = UNKNOWN;
	}

	return 0;
}

static int _send_request_build_tree_message(struct job_build_tree *self, struct spawn *spawn,
                                            int dest, int nhosts, si32 *hosts)
{
	int err;
	struct message_header             header;
	struct message_request_build_tree msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;        /* Always the same */
	header.dst   = dest;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_REQUEST_BUILD_TREE;

	msg.nhosts = nhosts;
	msg.hosts  = hosts;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _send_response_build_tree_message(struct job_build_tree *self, struct spawn *spawn)
{
	int err;
	struct message_header              header;
	struct message_response_build_tree msg;

	if (-1 == spawn->parent)
		return 0;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;        /* Always the same */
	header.dst   = spawn->parent;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_RESPONSE_BUILD_TREE;

	msg.deads = 0;	/* FIXME */

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _job_task_ctor(struct job_task *self, struct alloc *alloc,
                          const char *path, int argc, char **argv,
                          ui16 channel)
{
	int err;

	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_TASK;
	self->job.work  = _task_work;

	err = xstrdup(alloc, path, &self->path);
	if (unlikely(err)) {
		fcallerror("xstrdup", err);
		return err;
	}

	self->channel = channel;
	self->argc    = argc;
	self->task    = NULL;
	self->phase   = 1;
	self->acks    = 0;

	err = array_of_str_dup(alloc, argc + 1, argv, &self->argv);
	if (unlikely(err)) {
		fcallerror("array_of_str_dup", err);
		return err;
	}

	list_ctor(&self->job.list);

	return 0;
}

static int _job_task_dtor(struct job_task *self)
{
	int err;
	struct alloc *alloc = self->job.alloc;

	err = strfree(alloc, &self->path);
	if (unlikely(err)) {
		fcallerror("strfree", err);
		return err;
	}

	err = array_of_str_free(alloc, self->argc + 1, &self->argv);
	if (unlikely(err)) {
		fcallerror("array_of_str_free", err);
		return err;
	}

	return 0;
}

static int _free_job_task(struct alloc *alloc, struct job_task **self)
{
	int err;

	err = _job_task_dtor(*self);
	if (unlikely(err))
		fcallerror("_job_task_dtor", err);

	err = ZFREE(alloc, (void **)self, 1,
	            sizeof(struct job_task), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _task_work(struct job *job, struct spawn *spawn, int *completed)
{
	int err;
	int ret;
	struct job_task *self = (struct job_task *)job;

	/* Phase 1: Start the task
	 */
	if (1 == self->phase) {
		if (0 == spawn->tree.here) {
			err = _task_send_request(spawn, self->path,
			                         self->argc, self->argv,
			                         self->channel);
			if (unlikely(err))
				fcallerror("_task_send_request", err);
		}

		err = ZALLOC(spawn->alloc, (void **)&self->task,
		             1, sizeof(struct task), "");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		err = task_ctor(self->task, spawn->alloc, spawn,
		                self->path,
		                self->argc, self->argv,
		                self->channel);
		if (unlikely(err)) {
			fcallerror("task_ctor", err);
			return err;
		}

		err = task_start(self->task);
		if (unlikely(err)) {
			fcallerror("task_start", err);
			return err;
		}

		self->phase = 2;
	}

	/* Phase 2: Wait for the local task to finish.
	 */
	if (2 == self->phase) {
		if (task_is_done(self->task)) {
			self->phase = 3;

			err = task_thread_join(self->task);
			if (unlikely(err))
				fcallerror("task_thread_join", err);

			ret = task_exit_code(self->task);

			log("Task finished with exit code %d.", ret);

			err = task_dtor(self->task);
			if (unlikely(err))
				fcallerror("task_dtor", err);

			err = ZFREE(spawn->alloc, (void **)&self->task,
			             1, sizeof(struct task), "");
			if (unlikely(err)) {
				fcallerror("ZFREE", err);
				return err;
			}

			err = _task_send_response(spawn, ret);
			if (unlikely(err)) {
				fcallerror("_task_send_response", err);
				return err;
			}
		}
	}

	/* Phase 3: Wait for the tasks executed by children
	 */
	if (3 == self->phase) {
		if (spawn->nprocs == self->acks) {
			*completed = 1;

			log("All children finished executing the task.");
		}
	}

	return 0;
}

static int _task_send_request(struct spawn *spawn, const char *path,
                              int argc, char **argv, ui16 channel)
{
	int err;
	struct message_header       header;
	struct message_request_task msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_BCAST;
	header.type  = MESSAGE_TYPE_REQUEST_TASK;

	msg.path    = path;
	msg.argc    = argc;
	msg.argv    = argv;
	msg.channel = channel;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _task_send_response(struct spawn *spawn, int ret)
{
	int err;
	struct message_header        header;
	struct message_response_task msg;

	if (-1 == spawn->parent)
		return 0;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.dst   = spawn->parent;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_RESPONSE_TASK;

	msg.ret = ret;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _job_exit_ctor(struct job_exit *self, struct alloc *alloc,
                          const struct timespec *timeout)
{
	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_EXIT;
	self->job.work  = _exit_work;

	self->acks    = 0;
	self->timeout = *timeout;
	self->phase   = 1;

	list_ctor(&self->job.list);

	return 0;
}

static int _job_exit_dtor(struct job_exit *self)
{
	return 0;
}

static int _free_job_exit(struct alloc *alloc, struct job_exit **self)
{
	int err;

	err = _job_exit_dtor(*self);
	if (unlikely(err))
		fcallerror("_job_exit_dtor", err);

	err = ZFREE(alloc, (void **)self, 1,
	            sizeof(struct job_exit), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _exit_work(struct job *job, struct spawn *spawn, int *completed)
{
	int err;
	struct job_exit *self = (struct job_exit *)job;

	if (1 == self->phase) {
		/* Instead of sending the REQUEST_EXIT from node to node
		 * it is easier to let the master process broadcast the
		 * message. The master process is anyway the only process
		 * allowed to request an exit.
		 */
		if (0 == spawn->tree.here) {
			err = _exit_send_request(spawn);
			if (unlikely(err))
				fcallerror("_exit_send_request", err);
		}

		self->phase = 2;
	}

	if (2 == self->phase) {
		/* FIXME Check the timeout!
		 */

		if (spawn->nprocs == self->acks) {
			*completed = 1;

			log("All children exited.");

			err = _exit_send_response(spawn);
			if (unlikely(err))
				fcallerror("_exit_send_response", err);

			err = spawn_comm_flush(spawn);
			if (unlikely(err))
				fcallerror("spawn_comm_flush", err);
			exit(err);	/* FIXME Make this more graceful.
					 */
		}
	}

	return 0;
}

static int _exit_send_request(struct spawn *spawn)
{
	int err;
	struct message_header       header;
	struct message_request_exit msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_BCAST;
	header.type  = MESSAGE_TYPE_REQUEST_EXIT;

	debug("Broadcasting exit request.");

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _exit_send_response(struct spawn *spawn)
{
	int err;
	struct message_header        header;
	struct message_response_exit msg;

	if (-1 == spawn->parent)
		return 0;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.dst   = spawn->parent;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_RESPONSE_EXIT;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _prepare_task_job(struct spawn *spawn)
{
	int err;
	struct job *job;
	const char *plugin;
	const char *args;
	int n, i, j, argc;
	char **argv;
	char *p;
	ui16 channel;

	plugin = optpool_find_by_key(spawn->opts, "TaskPlugin");
	if (unlikely(!plugin)) {
		error("Missing 'TaskPlugin' option.");
		return -EINVAL;
	}

	/* TODO A disadvantage of splitting the string ourselves is that
	 *      it is tricky to be completely bash conforming. For example,
	 *      the algorithm below will not take quotes into account.
	 */

	args = optpool_find_by_key(spawn->opts, "TaskArgv");

	argc = 0;
	if (likely(args)) {
		/* Strip initial whitespaces from the string.
		 */
		while ((*args) && isspace(*args)) ++args;

		i = 0;
		while (args[i]) {
			/* Skip the word. */
			while (args[i] && (!isspace(args[i]))) ++i;
			/* Skip whitespaces. */
			while (args[i] &&   isspace(args[i]) ) ++i;

			if (args[i])
				++argc;
		}

		/* Above we counted the number of whitespace holes in
		 * the string so the number of arguments is that number
		 * plus one.
		 */
		++argc;
	}

	err = ZALLOC(spawn->alloc, (void **)&argv, (argc + 1), sizeof(char *), "");

	argv[0] = NULL;
	j = 0;
	if (likely(args)) {
		err = xstrdup(spawn->alloc, args, &p);
		if (unlikely(err)) {
			fcallerror("xstrdup", err);
			return err;
		}

		n = strlen(p);

		i = 0;
		while (p[i]) {
			argv[j++] = &p[i];

			/* skip word */
			while (p[i] && (!isspace(p[i]))) ++i;
			/* skip whitespaces */
			while (p[i] &&   isspace(p[i]) )
				p[i++] = 0;
		}
	}

	err = spawn_comm_resv_channel(spawn, &channel);
	if (unlikely(err)) {
		fcallerror("spawn_comm_alloc_channel", err);
		channel = 1;
	}


	err = alloc_job_task(spawn->alloc, plugin,
	                     argc, argv,
	                     channel, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_task", err);
		return err;
	}

	if (likely(argc)) {
		err = ZFREE(spawn->alloc, (void **)&argv[0], (n + 1), sizeof(char), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	err = ZFREE(spawn->alloc, (void **)&argv, (argc + 1), sizeof(char *), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	list_insert_before(&spawn->jobs, &job->list);

	return 0;
}

