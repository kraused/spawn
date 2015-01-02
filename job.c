
#include <string.h>
#include <stdio.h>

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

#include "devel.h"


static int _job_build_tree_ctor(struct job_build_tree *self, struct alloc* alloc,
                                struct spawn *spawn, int nhosts, const char **hosts);
static int _job_build_tree_dtor(struct job_build_tree *self);
static int _free_job_build_tree(struct alloc *alloc, struct job_build_tree **self);
static int _build_tree_work(struct job *job, struct spawn *spawn, int *completed);
static int _build_tree_spawn_children(struct job_build_tree *self, struct spawn *spawn);
static int _send_request_build_tree_message(struct job_build_tree *self, struct spawn *spawn,
                                            int dest, int nhosts, char **hosts);
static int _send_response_build_tree_message(struct job_build_tree *self, struct spawn *spawn);
static int _job_join_ctor(struct job_join *self, struct alloc *alloc, int parent);
static int _job_join_dtor(struct job_join *self);
static int _free_job_join(struct alloc *alloc, struct job_join **self);
static int _join_work(struct job *job, struct spawn *spawn, int *completed);
static int _join_send_request(struct spawn *spawn, int parent);
static int _job_task_ctor(struct job_task *self, struct alloc *alloc,
                          const char *path, int channel);
static int _job_task_dtor(struct job_task *self);
static int _free_job_task(struct alloc *alloc, struct job_task **self);
static int _task_work(struct job *job, struct spawn *spawn, int *completed);
static int _task_send_request(struct spawn *spawn, const char *path, int channel);
static int _sockaddr(int fd, ui32 *ip, ui32 *portnum);


int alloc_job_build_tree(struct alloc *alloc, struct spawn *spawn,
                         int nhosts, const char **hosts, struct job **self)
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

int alloc_job_join(struct alloc *alloc, int parent, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_join), "struct job_join");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_join_ctor((struct job_join *)*self, alloc, parent);
}

int alloc_job_task(struct alloc *alloc, const char* path,
                   int channel, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_task), "struct job_task");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_task_ctor((struct job_task *)*self, alloc, path, channel);
}

int free_job(struct job **self)
{
	int err;

	switch ((*self)->type) {
	case JOB_TYPE_BUILD_TREE:
		err = _free_job_build_tree((*self)->alloc,
		                           (struct job_build_tree **)self);
		break;
	case JOB_TYPE_JOIN:
		err = _free_job_join((*self)->alloc,
		                     (struct job_join **)self);
		break;
	case JOB_TYPE_TASK:
		err = _free_job_task((*self)->alloc,
		                            (struct job_task **)self);
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
                                struct spawn *spawn, int nhosts, const char **hosts)
{
	int err, tmp;
	int i, quot;

	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_BUILD_TREE;
	self->job.work  = _build_tree_work;

	list_ctor(&self->job.list);

	self->alloc  = alloc;
	self->phase  = 1;
	self->nhosts = nhosts;

	err = ZALLOC(alloc, (void **)&self->hosts, self->nhosts,
	             sizeof(char *), "hosts");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	for (i = 0; i < self->nhosts; ++i) {
		err = xstrdup(alloc, hosts[i], &self->hosts[i]);
		if (unlikely(err)) {
			fcallerror("xstrdup", err);
			goto fail1;
		}
	}

	self->nchildren = MIN(devel_tree_width, self->nhosts);
	quot = self->nhosts/self->nchildren;

	err = ZALLOC(alloc, (void **)&self->children, self->nchildren,
	             sizeof(struct _job_build_tree_child), "children");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		goto fail2;
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

fail2:
	tmp = ZFREE(self->alloc, (void **)&self->children, self->nchildren,
	            sizeof(struct _job_build_tree_child), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

fail1:
	for (i = 0; i < self->nhosts; ++i) {
		if (unlikely(!self->hosts[i]))
			continue;

		tmp = ZFREE(self->alloc, (void **)&self->hosts[i],
		            (strlen(self->hosts[i]) + 1), sizeof(char), "");
		if (unlikely(tmp))
			fcallerror("ZFREE", tmp);
	}

	tmp = ZFREE(self->alloc, (void **)&self->hosts, self->nhosts,
	            sizeof(char *), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _job_build_tree_dtor(struct job_build_tree *self)
{
	int err;
	int i;

	err = ZFREE(self->alloc, (void **)&self->children, self->nchildren,
	            sizeof(struct _job_build_tree_child), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	for (i = 0; i < self->nchildren; ++i) {
		err = ZFREE(self->alloc, (void **)&self->hosts[i],
		            (strlen(self->hosts[i]) + 1), sizeof(char), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	err = ZFREE(self->alloc, (void **)&self->hosts, self->nhosts,
	            sizeof(char *), "");
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

		if (0 == spawn->tree.here) {
			err = exec_worker_pool_start(spawn->wkpool);
			if (unlikely(err)) {
				fcallerror("exec_worker_pool_start", err);
				die();	/* FIXME */
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
					die();
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
			struct job *job;

			err = exec_worker_pool_stop(spawn->wkpool);
			if (unlikely(err)) {
				fcallerror("exec_worker_pool_stop", err);
				die();	/* FIXME */
			}

			/* FIXME Channel allocation.
			 */
			err = alloc_job_task(spawn->alloc, TASK_PLUGIN, 1, &job);
			if (unlikely(err)) {
				fcallerror("alloc_job_task", err);
				die();	/* FIXME */
			}

			list_insert_before(&spawn->jobs, &job->list);
		}

		log("Finished building the tree after %lld second(s).", llnow() - self->start);
	}

	return 0;
}

static int _build_tree_spawn_children(struct job_build_tree *self, struct spawn *spawn)
{
	int err;
	int i;
	ui32 ip, portnum;
	struct in_addr in;
	struct message_header       header;
	struct message_request_exec msg;
	char host[32];
	char argv1[32];
	char argv2[32];
	char argv3[32];
	char argv4[32];
	char argv5[32];
	char *argv[] = {SPAWN_EXE_OTHER,
	                argv1, argv2,
	                argv3, argv4,
	                argv5, NULL};

	err = _sockaddr(spawn->tree.listenfd, &ip, &portnum);
	if (unlikely(err)) {
		fcallerror("_sockaddr", err);
		return err;
	}

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

		snprintf(host, sizeof(host), self->hosts[self->children[i].host]);

		in.s_addr = htonl(ip);
		snprintf(argv1, sizeof(argv1), "%s", inet_ntoa(in));
		snprintf(argv2, sizeof(argv2), "%d", (int )portnum);

		snprintf(argv3, sizeof(argv3), "%d", spawn->tree.here);	/* my participant id */
		snprintf(argv4, sizeof(argv4), "%d", spawn->nhosts);	/* number of hosts */
		snprintf(argv5, sizeof(argv5), "%d", self->children[i].id);

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
                                            int dest, int nhosts, char **hosts)
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

static int _job_join_ctor(struct job_join *self, struct alloc *alloc, int parent)
{
	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_JOIN;
	self->job.work  = _join_work;

	self->parent    = parent;
	self->acked     = 0;

	list_ctor(&self->job.list);

	return 0;
}

static int _job_join_dtor(struct job_join *self)
{
	return 0;
}

static int _free_job_join(struct alloc *alloc, struct job_join **self)
{
	int err;

	err = _job_join_dtor(*self);
	if (unlikely(err))
		fcallerror("_job_join_dtor", err);

	err = ZFREE(alloc, (void **)self, 1,
	            sizeof(struct job_join), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _join_work(struct job *job, struct spawn *spawn, int *completed)
{
	int err;
	struct job_join *self = (struct job_join *)job;

	if (-1 != self->parent) {
		err = _join_send_request(spawn, self->parent);
		if (unlikely(err)) {
			fcallerror("_join_send_request", err);
			return err;
		}

		self->parent = -1;
	}

	if (1 == self->acked) {
		log("Succesfully joined the network.");
		*completed = 1;
	}

	return 0;
}

static int _join_send_request(struct spawn *spawn, int parent)
{
	int err;
	struct message_header       header;
	struct message_request_join msg;

	assert(1 == spawn->tree.nports);

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.dst   = parent;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_REQUEST_JOIN;

	msg.pid = getpid();

	err = _sockaddr(spawn->tree.ports[0], &msg.ip, &msg.portnum);
	if (unlikely(err))
		return err;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _job_task_ctor(struct job_task *self, struct alloc *alloc,
                          const char *path, int channel)
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

	list_ctor(&self->job.list);

	return 0;
}

static int _job_task_dtor(struct job_task *self)
{
	int err;

	err = ZFREE(self->job.alloc, (void **)&self->path, strlen(self->path) + 1,
	            sizeof(char), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
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
	struct job_task *self = (struct job_task *)job;
	struct task *task;

	/* Either it works or it does not.
	 */
	*completed = 1;

	if (0 == spawn->tree.here) {
		err = _task_send_request(spawn, self->path, self->channel);
		if (unlikely(err))
			fcallerror("_task_send_request", err);
	}

	err = ZALLOC(spawn->alloc, (void **)&task,
	             1, sizeof(struct task), "");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	err = task_ctor(task, spawn->alloc, spawn, self->path, self->channel);
	if (unlikely(err)) {
		fcallerror("task_ctor", err);
		return err;
	}

	list_insert_before(&spawn->tasks, &task->list);

	err = task_start(task);
	if (unlikely(err)) {
		fcallerror("task_start", err);
		return err;
	}

	return 0;
}

static int _task_send_request(struct spawn *spawn, const char *path, int channel)
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
	msg.channel = channel;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

static int _sockaddr(int fd, ui32 *ip, ui32 *portnum)
{
	int err;
	struct sockaddr_in sa;
	socklen_t len;

	len = sizeof(sa);
	err = getsockname(fd, (struct sockaddr *)&sa, &len);
	if (unlikely(err < 0)) {
		error("getsockname() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	if (unlikely(len != sizeof(sa))) {
		error("Size mismatch.");
		return -ESOMEFAULT;
	}

	*ip      = ntohl(sa.sin_addr.s_addr);
	*portnum = ntohs(sa.sin_port);

	return 0;
}

