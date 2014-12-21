
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
#include "loop.h"
#include "spawn.h"
#include "comm.h"
#include "helper.h"
#include "protocol.h"
#include "job.h"

#include "devel.h"

static int _work_available(struct spawn *spawn);
static int _ping(struct spawn *spawn, int timeout);
static int _handle_accept(struct spawn *spawn, int newfd);
static int _handle_message(struct spawn *spawn, struct buffer *buffer);
static int _handle_jobs(struct spawn *spawn);
static int _handle_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer);
static int _send_response_join(struct spawn *spawn, int dest);
static int _peeraddr(int fd, ui32 *ip, ui32 *portnum);


int loop(struct spawn *spawn)
{
	int err;
	int newfd;
	struct buffer *buffer;
	struct timespec ts;


	_ping(spawn, 60);	/* First time nothing is send. */

	while (1) {
		/* FIXME Handle signals - At least on the master */

		_ping(spawn, 60);	/* FIXME timeout value */

		err = cond_var_lock_acquire(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_acquire", err);
			die();
		}

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;	/* FIXME timeout value */

		while (!_work_available(spawn)) {
			err = cond_var_timedwait(&spawn->comm.cond, &ts);
			if (-ETIMEDOUT == err)
				break;
			if (unlikely(err)) {
				fcallerror("cond_var_wait", err);
				die();
			}
		}

		_ping(spawn, 60);	/* FIXME timeout value */

		newfd = atomic_read(spawn->tree.newfd);
		buffer = NULL;

		err = comm_dequeue(&spawn->comm, &buffer);
		if (unlikely(err && (-ENOENT != err)))
			die();

		err = cond_var_lock_release(&spawn->comm.cond);
		if (unlikely(err)) {
			fcallerror("cond_var_lock_release", err);
			die();
		}

		if (-1 != newfd) {
			err = _handle_accept(spawn, newfd);
			if (unlikely(err))
				die();	/* FIXME */
		}

		if (buffer) {
			err = _handle_message(spawn, buffer);
			if (unlikely(err))
				die();	/* FIXME */
		}

		err = _handle_jobs(spawn);
		if (unlikely(err))
			die();	/* FIXME */
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

	now = llnow();

	if (unlikely(-1 == last)) {
		last = now;
		return 0;
	}

	if ((now - last) > timeout/2) {
		/* FIXME Send ping broadcast message. */

		return 0;
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

	err = cond_var_lock_release(&spawn->comm.cond);
	if (unlikely(err)) {
		fcallerror("cond_var_lock_release", err);
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
	int err;
	struct message_header header;

	err = unpack_message_header(buffer, &header);
	if (unlikely(err)) {
		fcallerror("unpack_message_header", err);
		die();	/* FIXME */
	}

	log("Received a %d message from %d.", header.type, header.src);

	switch (header.type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		_handle_join(spawn, &header, buffer);
		break;
	default:
		error("Dropping unexpected message of type %d from %d.", header.type, header.src);
	}

	err = buffer_pool_push(&spawn->bufpool, buffer);
	if (unlikely(err))
		fcallerror("buffer_pool_push", err);

	return 0;
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

static int _handle_join(struct spawn *spawn, struct message_header *header, struct buffer *buffer)
{
	int i;
	int err, tmp;
	ui32 ip, portnum;
	struct message_request_join msg;
	int found;

	err = unpack_message_payload(buffer, header, spawn->alloc, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		die();	/* FIXME ?*/
	}

	found = -1;

	for (i = 0; i < spawn->tree.nports; ++i) {
		err = _peeraddr(spawn->tree.ports[i], &ip, &portnum);
		if (unlikely(err))
			continue;

		if ((ip == msg.ip) && (portnum == msg.portnum)) {
			found = i;
			break;
		}
	}

	if (unlikely(found < 0)) {
		error("Failed to match address with port number.");
		die();
	}

	log("Routing messages to %d via port %d.", (int )header->src, found);

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

	tmp = header->src;
	err = network_modify_lft(&spawn->tree, found, &tmp, 1);
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

	err = _send_response_join(spawn, header->src);
	if (unlikely(err)) {
		fcallerror("_send_response_join", err);
		return err;
	}

	return 0;
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

	err = spawn_send_message(spawn, &header, (void *)&msg);
	if (unlikely(err)) {
		fcallerror("spawn_send_message", err);
		return err;
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

