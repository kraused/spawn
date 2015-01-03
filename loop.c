
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <fcntl.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "loop.h"
#include "spawn.h"
#include "comm.h"
#include "helper.h"
#include "protocol.h"
#include "job.h"
#include "watchdog.h"
#include "plugin.h"


static int _work_available(struct spawn *spawn);
static int _ping(struct spawn *spawn, int timeout);
static int _send_ping(struct spawn *spawn, ll now);
static int _handle_accept(struct spawn *spawn, int newfd);
static int _handle_message(struct spawn *spawn, struct buffer *buffer);
static int _handle_jobs(struct spawn *spawn);
static int _handle_request_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_response_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _send_response_join(struct spawn *spawn, int dest);
static int _handle_ping(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_request_exec(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _alloc_exec_work_item(struct exec_worker_pool *wkpool,
                                 struct message_header *header,
                                 struct message_request_exec *msg,
                                 struct exec_work_item **wkitem);
static int _handle_request_build_tree(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_response_build_tree(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_request_task(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _find_peerport(struct spawn *spawn, ui32 ip, ui32 portnum);
static int _fix_lft(struct spawn *spawn, int port, int dest);
static int _peeraddr(int fd, ui32 *ip, ui32 *portnum);
static int _declare_child_alive(struct job_build_tree *job, int id);
static int _declare_child_ready(struct job_build_tree *job, int id);
static void _sighandler(int signum);
static int _install_sighandler();
static int _quit(struct spawn *spawn);
static int _send_exit(struct spawn *spawn);

static int _finished = 0;
static int _sigrecvd = 0;


int loop(struct spawn *spawn)
{
	int err;
	int newfd;
	struct buffer *buffer;
	struct timespec ts;

	err = _install_sighandler();
	if (unlikely(err)) {
		fcallerror("_install_sighandler", err);
		die();
	}

	_ping(spawn, 60);	/* First time nothing is send. */

	while (1) {
		if (_finished) {
			err = _quit(spawn);
			if(unlikely(err))
				fcallerror("_quit", err);

			break;
		}

		err = _handle_jobs(spawn);
		if (unlikely(err))
			die();	/* FIXME */

		_ping(spawn, 60);	/* FIXME timeout value */

		err = cond_var_lock_acquire(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_acquire", err);
			die();
		}

		/* FIXME It might make sense to keep the timeout very low
		 *       in the beginning and later increase it when all
		 *       tasks are finished and we rarely (or never?) have
		 *       to handle jobs.
		 */

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;	/* FIXME timeout value */

		while (!_work_available(spawn)) {
			err = cond_var_timedwait(&spawn->comm.cond, &ts);
			if (-ETIMEDOUT == err)
				break;
			if (unlikely(err)) {
				fcallerror("cond_var_timedwait", err);
				die();
			}
		}

		_ping(spawn, 60);	/* FIXME timeout value */

		newfd = atomic_read(spawn->tree.newfd);
		buffer = NULL;

		err = comm_dequeue(&spawn->comm, &buffer);
		if (unlikely(err && (-ENOENT != err)))
			die();	/* FIXME */

		err = cond_var_lock_release(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_release", err);
			die();
		}

		if (-1 != newfd) {
			err = _handle_accept(spawn, newfd);
			if (unlikely(err)) {
				fcallerror("_handle_accept", err);
				die();	/* FIXME */
			}
		}

		if (buffer) {
			err = _handle_message(spawn, buffer);
			if (unlikely(err)) {
				fcallerror("_handle_message", err);
				die();	/* FIXME */
			}
		}

		/* FIXME
		 * Check tasks for termination
		 */

		/* FIXME
		 * Terminate if there are no jobs left and no tasks running.
		 */
	}

	return 0;
}


static int _work_available(struct spawn *spawn)
{
	int err;
	int result;

	err = comm_dequeue_would_succeed(&spawn->comm, &result);
	if (unlikely(err)) {
		fcallerror("comm_dequeue_would_succeed", err);
		return 1;	/* Give it a try. */
	}

	return (-1 != atomic_read(spawn->tree.newfd) || result);
}

/*
 * Send a keep alive message to all other processes.
 */
static int _ping(struct spawn *spawn, int timeout)
{
	static ll last = -1;
	ll now;
	int err;

	if (0 != spawn->tree.here)
		return 0;

	now = llnow();

	if (unlikely(-1 == last)) {
		last = now;
		return 0;
	}

	if ((now - last) > timeout/2) {
		err = _send_ping(spawn, now);
		if (unlikely(err))
			fcallerror("_send_ping", err);

		last = now;
		return 0;
	}

	return -ESOMEFAULT;
}

static int _send_ping(struct spawn *spawn, ll now)
{
	int err;
	struct message_header   header;
	struct message_ping 	msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_BCAST;
	header.type  = MESSAGE_TYPE_PING;

	msg.now = now;

	debug("Sending ping message.");

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

/*
 * TODO Keep the ports in a local array. Insert them at a later
 *      point all at once!
 */
static int _handle_accept(struct spawn *spawn, int newfd)
{
	int err;
	int tmp;

	tmp = atomic_cmpxchg(spawn->tree.newfd, newfd, -1);
	if (unlikely(newfd != tmp)) {
		error("Detected unexpected write to tree.newfd.");
		die();
	}

	/* Temporarily disable the communication thread. Otherwise it
	 * happens that we have to wait for seconds before acquiring the
	 * lock.
	 */
	err = comm_stop_processing(&spawn->comm);
	if (unlikely(err)) {
		error("Failed to temporarily stop the communication thread.");
	}

	err = network_lock_acquire(&spawn->tree);
	if (unlikely(err))
		die();

	err = network_add_ports(&spawn->tree, &newfd, 1);
	if (unlikely(err)) {
		fcallerror("network_add_ports", err);
		die();
	}

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

static int _handle_message(struct spawn *spawn, struct buffer *buffer)
{
	int err, tmp;
	struct message_header header;

	err = unpack_message_header(buffer, &header);
	if (unlikely(err)) {
		fcallerror("unpack_message_header", err);
		die();	/* FIXME */
	}

	debug("Received a %d message from %d.", header.type, header.src);

	switch (header.type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = _handle_request_join(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_RESPONSE_JOIN:
		err = _handle_response_join(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_PING:
		err = _handle_ping(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_REQUEST_EXEC:
		err = _handle_request_exec(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _handle_request_build_tree(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _handle_response_build_tree(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_REQUEST_TASK:
		err = _handle_request_task(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_EXIT:
		err = _handle_exit(spawn, &header, buffer);
		break;
	default:
		error("Dropping unexpected message of type %d from %d.", header.type, header.src);
		err = -ESOMEFAULT;
	}

	if (unlikely(err)) {
		error("Message handler failed with error %d.", err);
		goto fail;
	}

	err = buffer_pool_push(&spawn->bufpool, buffer);
	if (unlikely(err)) {
		fcallerror("buffer_pool_push", err);
		return err;
	}

	return 0;

fail:
	tmp = buffer_pool_push(&spawn->bufpool, buffer);
	if (unlikely(tmp))
		fcallerror("buffer_pool_push", tmp);

	return err;
}

static int _handle_jobs(struct spawn *spawn)
{
	int err;
	int completed;
	struct list *p;
	struct list *q;
	struct job  *job;

	LIST_FOREACH_S(p, q, &spawn->jobs) {
		job = LIST_ENTRY(p, struct job, list);

		if (unlikely(!job->work)) {
			error("Skipping invalid job with NULL == work.");
			continue;
		}

		completed = 0;

		err = job->work(job, spawn, &completed);
		if (unlikely(err))
			fcallerror("job->work", err);
		if (completed) {
			list_remove(&job->list);

			err = free_job(&job);
			if (unlikely(err))
				fcallerror("free_job", err);
		}
	}

	return 0;
}

static int _handle_request_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_request_join msg;
	int port, dest;
	struct list *p;
	struct job *job;
	int matches;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	dest = header->src;
	port = _find_peerport(spawn, msg.ip, msg.portnum);
	if (unlikely(port < 0)) {
		error("Failed to match address with port number.");
		die();
	}

	debug("Routing messages to %d via port %d.", dest, port);

	err = _fix_lft(spawn, port, dest);
	if (unlikely(err)) {
		fcallerror("_fixup_lft", err);
		goto fail;
	}

	err = _send_response_join(spawn, dest);
	if (unlikely(err)) {
		fcallerror("_send_response_join", err);
		goto fail;
	}

	matches = 0;

	LIST_FOREACH(p, &spawn->jobs) {
		job = LIST_ENTRY(p, struct job, list);

		if (JOB_TYPE_BUILD_TREE == job->type) {
			err = _declare_child_alive((struct job_build_tree *)job, header->src);
			if (unlikely(err)) {
				fcallerror("_declare_child_alive", err);
				goto fail;
			}

			++matches;
		}
	}

	if (1 != matches) {
		error("Found %d jobs of type JOB_TYPE_JOIN instead of just one.", matches);
		err = -ESOMEFAULT;
		goto fail;
	}

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _handle_response_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct list *p;
	struct job *job;
	struct message_response_join msg;
	int matches;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	if (unlikely(msg.addr != spawn->tree.here)) {
		error("MESSAGE_TYPE_RESPONSE_JOIN message contains incorrect address %d.", (int )msg.addr);
		die();  /* Makes no sense to continue. */
	}

	spawn->opts = msg.opts;
	msg.opts    = NULL;

	matches = 0;

	LIST_FOREACH(p, &spawn->jobs) {
		job = LIST_ENTRY(p, struct job, list);

		if (JOB_TYPE_JOIN == job->type) {
			((struct job_join *)job)->acked = 1;
			++matches;
		}
	}

	if (1 != matches) {
		error("Found %d jobs of type JOB_TYPE_JOIN instead of just one.", matches);
		err = -ESOMEFAULT;
		goto fail;
	}

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _send_response_join(struct spawn *spawn, int dest)
{
	int err;
	struct message_header        header;
	struct message_response_join msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.dst   = dest;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_RESPONSE_JOIN;

	msg.addr = dest;
	msg.opts = spawn->opts;

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	msg.opts = NULL;

	return 0;
}

static int _handle_ping(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	return calm_the_watchdog();
}

static int _handle_request_exec(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_request_exec msg;
	struct exec_work_item *wkitem;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	err = _alloc_exec_work_item(spawn->wkpool, header, &msg, &wkitem);
	if (unlikely(err)) {
		fcallerror("_alloc_exec_work_item", err);
		goto fail;
	}

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	/* Try to insert the worker item. Spin as long as necessary if the queue is
	 * currently full.
	 */
	do {
		err = exec_worker_pool_enqueue(spawn->wkpool, wkitem);
		if (-ENOMEM == err)
			/* FIXME sched_yield()? */
			continue;
		if (unlikely(err)) {
			fcallerror("exec_worker_pool_enqueue", err);
			return err;
		}
	} while (err);

	return 0;

fail:
	assert(err);

	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _alloc_exec_work_item(struct exec_worker_pool *wkpool,
                                 struct message_header *header,
                                 struct message_request_exec *msg,
                                 struct exec_work_item **wkitem)
{
	int err, tmp;

	err = ZALLOC(wkpool->alloc, (void **)wkitem, 1, sizeof(struct exec_work_item), "work item");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	(*wkitem)->argc   = msg->argc;
	(*wkitem)->client = header->src;

	err = xstrdup(wkpool->alloc, msg->host, &(*wkitem)->host);
	if (unlikely(err)) {
		fcallerror("xstrdup", err);
		goto fail1;
	}

	err = array_of_str_dup(wkpool->alloc, (msg->argc + 1),
	                       (const char **)msg->argv, &(*wkitem)->argv);
	if (unlikely(err)) {
		fcallerror("array_of_str_dup", err);
		goto fail2;
	}

	return 0;

fail2:
	assert(err);

	strfree(wkpool->alloc, (char **)&(*wkitem)->host);

fail1:
	assert(err);

	tmp = ZFREE(wkpool->alloc, (void **)wkitem, 1, sizeof(struct exec_work_item), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _handle_request_build_tree(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_request_build_tree msg;
	struct job *job;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	err = alloc_job_build_tree(spawn->alloc, spawn, msg.nhosts,
	                           msg.hosts, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_build_tree", err);
		goto fail;
	}

	list_insert_before(&spawn->jobs, &job->list);

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _handle_response_build_tree(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct list *p;
	struct job *job;
	struct message_response_build_tree msg;
	int matches;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	matches = 0;

	LIST_FOREACH(p, &spawn->jobs) {
		job = LIST_ENTRY(p, struct job, list);

		if (JOB_TYPE_BUILD_TREE == job->type) {
			err = _declare_child_ready((struct job_build_tree *)job, header->src);
			if (unlikely(err)) {
				fcallerror("_declare_child_alive", err);
				goto fail;
			}

			++matches;
		}
	}

	if (1 != matches) {
		error("Found %d jobs of type JOB_TYPE_JOIN instead of just one.", matches);
		err = -ESOMEFAULT;
		goto fail;
	}

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _handle_request_task(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_request_task msg;
	struct job *job;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	err = alloc_job_task(spawn->alloc, msg.path, msg.channel, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_task", err);
		goto fail;
	}

	list_insert_before(&spawn->jobs, &job->list);

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	return err;
}

static int _handle_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	_finished = 2;

	debug("Received exit message.");

	return 0;
}

static int _find_peerport(struct spawn *spawn, ui32 ip, ui32 portnum)
{
	int i;
	int found;
	int err;
	ui32 addr, port;

	found = -1;

	for (i = 0; i < spawn->tree.nports; ++i) {
		err = _peeraddr(spawn->tree.ports[i], &addr, &port);
		if (unlikely(err))
			continue;

		if ((ip == addr) && (portnum == port)) {
			found = i;
			break;
		}
	}

	return found;
}

static int _fix_lft(struct spawn *spawn, int port, int dest)
{
	int err;

	/* Temporarily disable the communication thread. Otherwise it
	 * happen that we wait for seconds before acquiring the lock.
	 */
	err = comm_stop_processing(&spawn->comm);
	if (unlikely(err)) {
		error("Failed to temporarily stop the communication thread.");
	}

	err = network_lock_acquire(&spawn->tree);
	if (unlikely(err))
		die();

	/* FIXME This is a tree. We know that header->src is responsible for a
	 *       full range of ports so we can fix the lft here for multiple ports.
	 */

	err = network_modify_lft(&spawn->tree, port, &dest, 1);
	if (unlikely(err))
		die();

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


static int _peeraddr(int fd, ui32 *ip, ui32 *portnum)
{
	int err;
	struct sockaddr_in sa;
	socklen_t len;

	len = sizeof(sa);
	err = getpeername(fd, (struct sockaddr *)&sa, &len);
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

static int _declare_child_alive(struct job_build_tree *job, int id)
{
	int i;
	int ok;

	ok = 0;

	for (i = 0; i < job->nchildren; ++i) {
		if (job->children[i].id == id) {
			if (unlikely(!job->children[i].state == UNKNOWN)) {
				error("Incorrect state %d (expected UNKNOWN = %d)",
				      job->children[i].state, UNKNOWN);
			}

			job->children[i].state = ALIVE;
			ok = 1;
		}
	}

	if (unlikely(!ok)) {
		error("Found no matching children with id %d.", id);
		return -ESOMEFAULT;
	}

	return 0;
}

static int _declare_child_ready(struct job_build_tree *job, int id)
{
	int i;
	int ok;

	ok = 0;

	for (i = 0; i < job->nchildren; ++i) {
		if (job->children[i].id == id) {
			if (unlikely(!job->children[i].state == ALIVE)) {
				error("Incorrect state %d (expected ALIVE = %d)",
				      job->children[i].state, ALIVE);
			}

			job->children[i].state = READY;
			ok = 1;
		}
	}

	if (unlikely(!ok)) {
		error("Found no matching children with id %d.", id);
		return -ESOMEFAULT;
	}

	return 0;
}

static void _sighandler(int signum)
{
	_finished = 1;
	_sigrecvd = signum;
}

static int _install_sighandler()
{
	if (SIG_ERR == signal(SIGQUIT, _sighandler))
		return -errno;
	if (SIG_ERR == signal(SIGINT , _sighandler))
		return -errno;
	if (SIG_ERR == signal(SIGTERM, _sighandler))
		return -errno;

	return 0;
}

static int _quit(struct spawn *spawn)
{
	int err;

	if (1 == _finished) {
		err = _send_exit(spawn);
		if (unlikely(err))
			return err;
	}

	err = spawn_comm_flush(spawn);
	if (unlikely(err)) {
		fcallerror("spawn_comm_flush", err);
		return err;
	}

	return 0;
}

static int _send_exit(struct spawn *spawn)
{
	int err;
	struct message_header   header;
	struct message_exit 	msg;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = spawn->tree.here;	/* Always the same */
	header.flags = MESSAGE_FLAG_BCAST;
	header.type  = MESSAGE_TYPE_EXIT;

	debug("Sending exit message.");

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
	}

	return 0;
}

