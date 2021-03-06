
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
#include "msgbuf.h"
#include "task.h"


static int _work_available(struct spawn *spawn);
static int _ping(struct spawn *spawn, int timeout);
static int _send_ping(struct spawn *spawn, ll now);
static int _handle_accept(struct spawn *spawn, int newfd);
static int _handle_message(struct spawn *spawn, struct buffer *buffer);
static int _handle_jobs(struct spawn *spawn);
static int _handle_request_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static struct job_build_tree *_find_job_build_tree(struct spawn *spawn);
static int _insert_process_in_struct_spawn(struct spawn *spawn,
	                                   struct job_build_tree *job,
	                                   struct message_header *header,
	                                   struct message_request_join *msg,
	                                   int port);
static int _alloc_process_list_in_struct_spawn(struct spawn *spawn,
	                                       struct job_build_tree *job);
static struct process *_find_spawned_process_by_id(struct spawn *spawn, int id);
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
static int _handle_response_task(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static struct job_task *_find_job_task(struct spawn *spawn);
static int _handle_request_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_response_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_write_stdout(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_write_stderr(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _handle_user(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static struct job_exit *_find_job_exit(struct spawn *spawn);
static int _find_peerport(struct spawn *spawn, ui32 ip, ui32 portnum);
static int _fix_lft(struct spawn *spawn, int port, int *ids, int nids);
static int _peeraddr(int fd, ui32 *ip, ui32 *portnum);
static struct job_build_tree_child *_find_child_by_id(struct job_build_tree *job, int id);
static int _declare_child_alive(struct job_build_tree *job, int id);
static int _declare_child_ready(struct job_build_tree *job, int id);
static void _sighandler(int signum);
static int _install_sighandler();
static int _quit(struct spawn *spawn);
static struct job *_find_one_and_only_job(struct spawn *spawn, int type);
static int _flush_io_buffers(struct spawn *spawn);
static int _flush_io_buffer(struct spawn *spawn, struct msgbuf *buf, int type);

static int _finished = 0;
static int _sigrecvd = 0;


int loop(struct spawn *spawn)
{
	int err;
	int newfd;
	struct buffer *buffer;
	struct timespec timeout;
	struct timespec abstime;

	/* FIXME What kind of signal handling do we want to do
	 *       for the remote processes?
	 */

	if (0 == spawn->tree.here) {
		err = _install_sighandler();
		if (unlikely(err)) {
			fcallerror("_install_sighandler", err);
			die();
		}
	}

	_ping(spawn, 60);	/* First time nothing is send. */

	while (1) {
		if (0 == spawn->tree.here) {
			if (list_is_empty(&spawn->jobs))
				_finished = 1;

			if (1 == _finished) {
				err = _quit(spawn);
				if(unlikely(err))
					fcallerror("_quit", err);

				_finished = 2;
			}
		}

		err = _handle_jobs(spawn);
		if (unlikely(err))
			die();	/* FIXME */

		err = _flush_io_buffers(spawn);
		if (unlikely(err))
			fcallerror("_flush_io_buffer", err);

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
		timeout.tv_sec  = 0;
		timeout.tv_nsec = 1000L*1000L;	/* Millisecond */

		err = abstime_near_future(&timeout, &abstime);
		if (unlikely(err))
			fcallerror("abstime_near_future", err);

		while (!_work_available(spawn)) {
			err = cond_var_timedwait(&spawn->comm.cond, &abstime);
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
			if (unlikely(err))
				fcallerror("_handle_accept", err);
		}

		if (buffer) {
			err = _handle_message(spawn, buffer);
			if (unlikely(err))
				fcallerror("_handle_message", err);
		}
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
 * TODO Stopping and restarting the communication thread is a rather heavy
 *      operation. It might be advantageous to aggregate the connections and
 *      only insert a batch of them at once. This however raises new questions
 *      concerning the cost of the additional delay (we will not be able to
 *      receive the REQUEST_JOIN until we updated the lft) and a good choice
 *      of the batch size and timeout.
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
	if (unlikely(err))
		error("Failed to temporarily stop the communication thread.");

	err = network_lock_acquire(&spawn->tree);
	if (unlikely(err))
		die();

	debug("Adding new port %d to port list.", newfd);

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
		die();	/* This is pretty much impossible anyway since
			 * the communication thread had a look at the
			 * header to see how to treat the packet.
			 */
	}

	if ((header.type != MESSAGE_TYPE_WRITE_STDOUT) &&
	    (header.type != MESSAGE_TYPE_WRITE_STDERR))
		debug("Received a %d message from %d.", header.type, header.src);

	switch (header.type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = _handle_request_join(spawn, &header, buffer);
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
	case MESSAGE_TYPE_RESPONSE_TASK:
		err = _handle_response_task(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_REQUEST_EXIT:
		err = _handle_request_exit(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_RESPONSE_EXIT:
		err = _handle_response_exit(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_WRITE_STDOUT:
		err = _handle_write_stdout(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_WRITE_STDERR:
		err = _handle_write_stderr(spawn, &header, buffer);
		break;
	case MESSAGE_TYPE_USER:
		err = _handle_user(spawn, &header, buffer);
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

		/* FIXME If job->work() fails we just try again and usually it
		 *       will fail again so we are just hanging in an infinite
		 *       loop.
		 */

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
	struct job_build_tree *job;

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

	job = _find_job_build_tree(spawn);
	if (unlikely(!job))
		goto fail;

	err = _insert_process_in_struct_spawn(spawn, job, header, &msg, port);
	if (unlikely(err)) {
		fcallerror("_insert_process_in_struct_spawn", err);
		goto fail;
	}

	debug("Routing messages to %2d via port %2d.", dest, port);

	err = _fix_lft(spawn, port, &dest, 1);
	if (unlikely(err)) {
		fcallerror("_fixup_lft", err);
		goto fail;
	}

	err = _send_response_join(spawn, dest);
	if (unlikely(err)) {
		fcallerror("_send_response_join", err);
		goto fail;
	}

	err = _declare_child_alive(job, header->src);
	if (unlikely(err)) {
		fcallerror("_declare_child_alive", err);
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

static struct job_build_tree *_find_job_build_tree(struct spawn *spawn)
{
	return (struct job_build_tree *)_find_one_and_only_job(spawn, JOB_TYPE_BUILD_TREE);
}

static int _insert_process_in_struct_spawn(struct spawn *spawn,
	                                   struct job_build_tree *job,
	                                   struct message_header *header,
	                                   struct message_request_join *msg,
	                                   int port)
{
	int err;
	struct process *p;

	if (0 == spawn->nprocs) {
		err = _alloc_process_list_in_struct_spawn(spawn, job);
		if (unlikely(err)) {
			fcallerror("_alloc_process_list_in_struct_spawn", err);
			die();	/* No reasonable way to continue.
				 */
		}
	}

	p = _find_spawned_process_by_id(spawn, header->src);
	if (unlikely(!p)) {
		error("_find_spawned_process_by_id() returned NULL.");
		return -ESOMEFAULT;
	}

	p->pid  = msg->pid;
	p->port = port;
	p->addr.ip      = msg->ip;
	p->addr.portnum = msg->portnum;

	return 0;
}

static int _alloc_process_list_in_struct_spawn(struct spawn *spawn,
	                                       struct job_build_tree *job)
{
	int err;
	int i;

	/* TODO This is only correct if we do not have any dead hosts.
	 */
	spawn->nprocs = job->nchildren;

	err = ZALLOC(spawn->alloc, (void **)&spawn->procs, spawn->nprocs,
	             sizeof(struct process), "procs");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	for (i = 0; i < spawn->nprocs; ++i) {
		spawn->procs[i].id   = job->children[i].id;
		spawn->procs[i].port = -1;
	}

	return 0;
}

static struct process *_find_spawned_process_by_id(struct spawn *spawn, int id)
{
	int i;
	struct process *p;

	p = NULL;
	for (i = 0; i < spawn->nprocs; ++i)
		if (spawn->procs[i].id == id) {
			p = &spawn->procs[i];
			break;
		}

	return p;
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
	                       msg->argv, &(*wkitem)->argv);
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
	struct job_build_tree *job;
	struct message_response_build_tree msg;
	int i;
	struct process *p;
	struct job_build_tree_child *child;
	si32 *ids;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	job = _find_job_build_tree(spawn);
	if (unlikely(!job))
		goto fail;

	err = _declare_child_ready(job, header->src);
	if (unlikely(err)) {
		fcallerror("_declare_child_alive", err);
		goto fail;
	}

	p = _find_spawned_process_by_id(spawn, header->src);
	if (unlikely(!p)) {
		error("_find_process_by_id() returned NULL");
		goto fail;
	}
	if (unlikely(p->port < 0)) {
		error("Port is negative.");
		goto fail;
	}

	child = _find_child_by_id(job, header->src);
	if (unlikely(!child)) {
		error("_find_child_by_id() returned NULL");
		goto fail;
	}

	/* We already set the LFT entry for the host itself so the +1 is fine.
	 */
	ids = job->hosts + child->host + 1;

	for (i = 0; i < child->nhosts; ++i)
		debug("Routing messages to %2d via port %2d.", ids[i] + 1, p->port);

	/* The root process is not accounted for in the host list in struct spawn
	 * so we need to add +1 to map from the host id to the network participant id.
	 */
	for (i = 0; i < child->nhosts; ++i)
		ids[i]++;
	err = _fix_lft(spawn, p->port, ids, child->nhosts);
	for (i = 0; i < child->nhosts; ++i)
		ids[i]--;

	if (unlikely(err)) {
		fcallerror("_fixup_lft", err);
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

	err = alloc_job_task(spawn->alloc, msg.path,
	                     msg.argc, msg.argv,
	                     msg.channel, &job);
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

static int _handle_response_task(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_response_task msg;
	struct job_task *job;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	job = _find_job_task(spawn);
	if (unlikely(!job))
		goto fail;

	job->acks += 1;

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

static struct job_task *_find_job_task(struct spawn *spawn)
{
	return (struct job_task *)_find_one_and_only_job(spawn, JOB_TYPE_TASK);
}

static int _handle_request_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_request_exit msg;
	struct job *job;
	struct timespec timeout;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	/* FIXME Read from configuration
	 */
	timeout.tv_sec  = 3;
	timeout.tv_nsec = 0;

	err = alloc_job_exit(spawn->alloc, &timeout, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_exit", err);
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

static int _handle_response_exit(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct message_response_exit msg;
	struct job_exit *job;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	job = _find_job_exit(spawn);
	if (unlikely(!job))
		goto fail;

	job->acks += 1;

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

static int _handle_write_stdout(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err;
	struct message_write_stdout msg;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	fprintf(stdout, "%s", msg.lines);
	fflush (stdout);

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;
}

static int _handle_write_stderr(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err;
	struct message_write_stderr msg;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	fprintf(stderr, "%s", msg.lines);
	fflush (stderr);

	err = free_message_payload(header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("free_message_payload", err);
		return err;
	}

	return 0;
}

static int _handle_user(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int err, tmp;
	struct task_recvd_message *msg;
	struct job_task *job;

	err = ZALLOC(spawn->alloc, (void **)&msg, 1, sizeof(struct task_recvd_message), "msg");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		die();	/* FIXME ?*/
	}

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg->msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	job = _find_job_task(spawn);
	if (unlikely(!job))
		goto fail;

	if (unlikely(header->channel != job->channel)) {
		error("Mismatch between task channel %d and message channel %d.", job->channel, header->channel);
		goto fail;
	}

	/* TODO Retry?
	 */
	err = task_enqueue_message(job->task, msg);
	if (unlikely(err)) {
		fcallerror("task_enqueue_message", err);
		goto fail;
	}

	return 0;

fail:
	tmp = free_message_payload(header, spawn->alloc, (void *)msg);
	if (unlikely(tmp))
		fcallerror("free_message_payload", tmp);

	tmp = ZFREE(spawn->alloc, (void **)&msg, 1, sizeof(struct task_recvd_message), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static struct job_exit *_find_job_exit(struct spawn *spawn)
{
	return (struct job_exit *)_find_one_and_only_job(spawn, JOB_TYPE_EXIT);
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

static int _fix_lft(struct spawn *spawn, int port, int *ids, int nids)
{
	int err;

	/* Temporarily disable the communication thread. Otherwise it
	 * may happen that we wait for seconds before acquiring the lock.
	 */
	err = comm_stop_processing(&spawn->comm);
	if (unlikely(err))
		error("Failed to temporarily stop the communication thread.");

	err = network_lock_acquire(&spawn->tree);
	if (unlikely(err))
		die();

	err = network_modify_lft(&spawn->tree, port, ids, nids);
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

static struct job_build_tree_child *_find_child_by_id(struct job_build_tree *job, int id)
{
	int i;
	struct job_build_tree_child *child;

	child = NULL;
	for (i = 0; i < job->nchildren; ++i) {
		if (job->children[i].id == id) {
			child = &job->children[i];
		}
	}

	if (unlikely(!child))
		error("Found no matching children with id %d.", id);

	return child;
}

static int _declare_child_alive(struct job_build_tree *job, int id)
{
	struct job_build_tree_child *child;

	child = _find_child_by_id(job, id);
	if (unlikely(!child))
		return -ESOMEFAULT;

	if (unlikely(!child->state == UNKNOWN)) {
		error("Incorrect state %d (expected UNKNOWN = %d)",
		      child->state, UNKNOWN);
	}

	child->state = ALIVE;

	return 0;
}

static int _declare_child_ready(struct job_build_tree *job, int id)
{
	struct job_build_tree_child *child;

	child = _find_child_by_id(job, id);
	if (unlikely(!child))
		return -ESOMEFAULT;

	if (unlikely(!child->state == ALIVE)) {
		error("Incorrect state %d (expected ALIVE = %d)",
		      child->state, ALIVE);
	}

	debug("Declaring child %d ready.", child->id);

	child->state = READY;

	return 0;
}

static void _sighandler(int signum)
{
	/* Ignore subsequent signals. Only the first one counts.
	 */
	if (1 == _finished) {
		_finished = 2;
	}
	if (0 == _finished) {
		_finished = 1;
		_sigrecvd = signum;
	}
}

static int _install_sighandler()
{
	struct sigaction act, oact;

	act.sa_handler = _sighandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	if (sigaction(SIGQUIT, &act, &oact) < 0)
		return -errno;
	if (sigaction(SIGINT , &act, &oact) < 0)
		return -errno;
	if (sigaction(SIGTERM, &act, &oact) < 0)
		return -errno;

	return 0;
}

static int _quit(struct spawn *spawn)
{
	int err;
	struct job *job;
	struct timespec timeout;

	/* FIXME Read from configuration
	 */
	timeout.tv_sec  = 3;
	timeout.tv_nsec = 0;

	err = alloc_job_exit(spawn->alloc, &timeout, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_exit", err);
		return err;
	}

	list_insert_before(&spawn->jobs, &job->list);

	return 0;
}

static struct job *_find_one_and_only_job(struct spawn *spawn, int type)
{
	int matches;
	struct list *p;
	struct job *job;
	struct job *ret;

	matches = 0;

	LIST_FOREACH(p, &spawn->jobs) {
		job = LIST_ENTRY(p, struct job, list);

		if (type == job->type) {
			ret = job;
			++matches;
		}
	}

	if (1 != matches) {
		error("Found %d jobs of type %d instead of just one.", matches, type);
		ret = NULL;
	}

	return ret;
}

static int _flush_io_buffers(struct spawn *spawn)
{
	int err;

	if (spawn->bout) {
		err = _flush_io_buffer(spawn, spawn->bout, MESSAGE_TYPE_WRITE_STDOUT);
		if (unlikely(err))
			return err;
	}

	if (spawn->berr) {
		err = _flush_io_buffer(spawn, spawn->berr, MESSAGE_TYPE_WRITE_STDERR);
		if (unlikely(err))
			return err;
	}

	return 0;
}

static int _flush_io_buffer(struct spawn *spawn, struct msgbuf *buf, int type)
{
	int err;
	struct message_header       header;
	struct message_write_stderr msg;

	err = msgbuf_lock(buf);
	if (unlikely(err)) {
		fcallerror("msgbuf_lock", err);
		return err;
	}

	/* FIXME This section uses knowledge of the internal structure of struct msgbuf.
	 */
	{
		struct list *p;
		struct list *q;

		LIST_FOREACH_S(p, q, &buf->lines) {
			struct msgbuf_line *line = LIST_ENTRY(p, struct msgbuf_line, list);

			memset(&header, 0, sizeof(header));
			memset(&msg   , 0, sizeof(msg));

			header.src   = spawn->tree.here;        /* Always the same */
			header.flags = MESSAGE_FLAG_UCAST;
			header.type  = type;

			msg.lines = line->string;

			err = spawn_send_message(spawn, &header, (void *)&msg);
			if (unlikely(err)) {
				fcallerror("spawn_send_message", err);
				return err;
			}

			list_remove(p);

			err = ZFREE(buf->alloc, (void **)&line, sizeof(struct msgbuf_line), 1, "");
			if (unlikely(err))
				fcallerror("ZFREE", err);
		}
	}

	err = msgbuf_unlock(buf);
	if (unlikely(err)) {
		fcallerror("msgbuf_unlock", err);
		return err;
	}

	return 0;
}

