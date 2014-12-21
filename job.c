
#include <string.h>
#include <stdio.h>

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
#include "plugin.h"
#include "protocol.h"

#include "devel.h"


static int _job_build_tree_ctor(struct job_build_tree *self, struct alloc* alloc);
static int _job_build_tree_dtor(struct job_build_tree *self);
static int _free_job_build_tree(struct alloc *alloc, struct job_build_tree **self);
static int _build_tree_work(struct job *job, struct spawn *spawn, int *completed);
static int _job_join_ctor(struct job_join *self, struct alloc *alloc, int father);
static int _job_join_dtor(struct job_join *self);
static int _free_job_join(struct alloc *alloc, struct job_join **self);
static int _join_work(struct job *job, struct spawn *spawn, int *completed);
static int _join_send_request(struct spawn *spawn, int father);
static int _join_recv_response(struct spawn *spawn, int father);
static int _sockaddr(int fd, ui32 *ip, ui32 *portnum);


int alloc_job_build_tree(struct alloc *alloc, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_build_tree), "struct job_build_tree");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_build_tree_ctor((struct job_build_tree *)*self, alloc);
}

int alloc_job_join(struct alloc *alloc, int father, struct job **self)
{
	int err;

	err = ZALLOC(alloc, (void **)self, 1,
	             sizeof(struct job_join), "struct job_join");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return _job_join_ctor((struct job_join *)*self, alloc, father);
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


static int _job_build_tree_ctor(struct job_build_tree *self, struct alloc *alloc)
{
	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_BUILD_TREE;
	self->job.work  = _build_tree_work;

	list_ctor(&self->job.list);

	return 0;
}

static int _job_build_tree_dtor(struct job_build_tree *self)
{
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

	*completed = 0;

#if 0
/* ************************************************************ */
	char argv1[32];
	char argv2[32];
	char argv3[32];
	char argv4[32];
	char argv5[32];

	int i, n;
	int j;
	int err;
	struct in_addr in;
	ui32 ip, portnum;

	err = _sockaddr(spawn->tree.listenfd, &ip, &portnum);
	if (unlikely(err))
		die();

	in.s_addr = htonl(ip);

	snprintf(argv1, sizeof(argv1), "%s", inet_ntoa(in));
	snprintf(argv2, sizeof(argv2), "%d", (int )portnum);

	n = (spawn->nhosts + devel_tree_width - 1)/devel_tree_width;

	for (i = 0, j = 1; i < spawn->nhosts; i += n, ++j) {
		/* FIXME Threaded spawning! Take devel_fanout into account. */
		/* At the same time, while spawning we need to accept connections! */

		snprintf(argv3, sizeof(argv3), "%d", 0);		/* my participant id */
		snprintf(argv4, sizeof(argv4), "%d", spawn->nhosts);	/* size */
		snprintf(argv5, sizeof(argv5), "%d", i + 1);		/* their participant id */

		char *const argw[] = {SPAWN_EXE_OTHER,
		                      argv1, argv2,
		                      argv3, argv4,
		                      argv5, NULL};
		err = spawn->exec->ops->exec(spawn->exec, "localhost", argw);
		if (unlikely(err))
			die();
	}
/* ************************************************************ */
	
	*completed = 1;
#endif

	return 0;
}

static int _job_join_ctor(struct job_join *self, struct alloc *alloc, int father)
{
	self->job.alloc = alloc;
	self->job.type  = JOB_TYPE_JOIN;
	self->job.work  = _join_work;

	self->father    = father;

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

	*completed = 0;

	err = _join_send_request(spawn, self->father);
	if (unlikely(err)) {
		fcallerror("_join_send_request", err);
		return err;
	}

	err = _join_recv_response(spawn, self->father);
	if (unlikely(err)) {
		fcallerror("_join_recv_request", err);
		return err;
	}

	*completed = 1;

	return -ENOTIMPL;
}

static int _join(struct spawn *spawn, int father)
{

	return 0;
}

static int _join_send_request(struct spawn *spawn, int father)
{
	int err;
	struct message_header       header;
	struct message_request_join msg;

	assert(1 == spawn->tree.nports);

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.dst   = father;
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

static int _join_recv_response(struct spawn *spawn, int father)
{
	int err;
	struct buffer *buffer;
	struct message_header header;
	struct message_response_join msg;

	/* FIXME This is NOT the way to implement this. Let the main
	 *       loop handle the receiving and just test in here whether
	 *	 a reply arrived.
	 */

#if 0
	while (1) {
		err = cond_var_lock_acquire(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_acquire", err);
			die();
		}

		while (!_work_available(spawn)) {
			err = cond_var_wait(&spawn->comm.cond);
			if (unlikely(err)) {
				fcallerror("cond_var_wait", err);
				die();
			}
		}

		err = comm_dequeue(&spawn->comm, &buffer);
		if (-ENOENT == err) {
			error("comm_dequeue() returned -ENOENT.");
			goto unlock;
		}
		if (unlikely(err))
			die();

unlock:
		err = cond_var_lock_release(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_release", err);
			die();
		}

		err = unpack_message_header(buffer, &header);
		if (unlikely(err)) {
			fcallerror("unpack_message_header", err);
			goto push;
		}

		if (unlikely(MESSAGE_TYPE_RESPONSE_JOIN != header.type)) {
			error("Received unexpected message of type %d.", header.type);
			goto push;	/* Drop message and just continue.
					 */
		}

		err = unpack_message_payload(buffer, &header, spawn->alloc, (void *)&msg);
		if (unlikely(err)) {
			fcallerror("unpack_message_payload", err);
			goto push;
		}

		break;

push:
		err = buffer_pool_push(&spawn->bufpool, buffer);
		if (unlikely(err))
			fcallerror("buffer_pool_push", err);
	}

	if (unlikely(msg.addr != spawn->tree.here)) {
		error("MESSAGE_TYPE_RESPONSE_JOIN message contains incorrect address %d.", (int )msg.addr);
		die();	/* Makes no sense to continue.
			 */
	}

	log("Succesfully joined the network.");
#endif

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

