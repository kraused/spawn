
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "helper.h"
#include "spawn.h"
#include "plugin.h"
#include "pack.h"
#include "protocol.h"
#include "comm.h"
#include "atomic.h"
#include "helper.h"
#include "watchdog.h"

#include "devel.h"


/*
 * Some information is cumbersome to transfer via the wire since
 * they are necessary to have a properly prepared communication
 * module.
 */
struct _args_other
{
	/* Address of the father to connect to. Obviously needs
	 * to be passed via command line (or environment).
	 */
	struct sockaddr_in	sa;
	/* Network identifier of the father. Required for the
	 * header of the REQUEST_JOIN message.
	 */
	int			father;
	/* Size of the network. Required in order to setup the
	 * LFT.
	 */
	int			size;
	/* Network identifier of this process. Required for the
	 * header of the REQUEST_JOIN message.
	 */
	int			here;
};

static int _main_on_local(struct spawn *spawn, int argc, char **argv);
static int _main_on_other(struct spawn *spawn, int argc, char **argv);
static int _localaddr(struct sockaddr_in *sa);
static int _parse_argv_on_other(int argc, char **argv, struct _args_other *args);
static int _connect_to_father(struct spawn *spawn, struct sockaddr_in *sa);
static int _join(struct spawn *spawn);


int main(int argc, char **argv)
{
	struct alloc *alloc;
	struct spawn spawn;
	int n, m;
	int err;

	alloc = libc_allocator_with_debugging();

	err = spawn_ctor(&spawn, alloc);
	if (unlikely(err)) {
		error("struct spawn constructor failed with exit code %d.", err);
		return err;
	}

	/* This is a simple method to recognize whether to call
	 * _main_on_other or _main_on_local. On the remote side
	 * we always execute the libexec spawn executable using
	 * a full path (PATH is empty). If argv[0] ends with
	 * "libexec/spawn" we assume that _main_on_other is the
	 * next step. This should be accurate in 99% of the cases
	 * and in 100% of the cases where the application is
	 * used as intended.
	 */

	n = strlen(argv[0]);
	m = sizeof("libexec/spawn") - 1;

	if ((n >= m) && !strcmp(argv[0] + n - m, "libexec/spawn"))
		err = _main_on_other(&spawn, argc, argv);
	else
		err = _main_on_local(&spawn, argc, argv);

	if (unlikely(err)) {
		error("main function failed with exit code %d.", err);
		return err;
	}

	err = spawn_dtor(&spawn);
	if (unlikely(err)) {
		error("struct spawn destructor failed with exit code %d.", err);
		return err;
	}

	return err;
}


static int _main_on_local(struct spawn *spawn, int argc, char **argv)
{
	int err;
	struct sockaddr_in sa;

	/* FIXME parse command line arguments */

	err = spawn_setup_on_local(spawn, devel_nhosts, devel_hostlist, devel_tree_width);
	if (unlikely(err)) {
		error("Failed to setup spawn instance.");
		return err;
	}

	err = spawn_load_exec_plugin(spawn, EXEC_PLUGIN);
	if (unlikely(err))
		return err;

	_localaddr(&sa);

	err = spawn_bind_listenfd(spawn, (struct sockaddr *)&sa, sizeof(sa));
	if (unlikely(err)) {
		error("Failed to bind the listenfd.");
		return err;
	}

	err = spawn_comm_start(spawn);
	if (unlikely(err)) {
		error("Failed to start the communication module.");
		return err;
	}

/* ************************************************************ */
	char addr[32];
	char port[32];
	char argv1[32];
	char argv2[32];
	char argv3[32];

	int i, n;
	int j;
	int newfd, tmp;

	{
		socklen_t len = sizeof(sa);
		err = getsockname(spawn->tree.listenfd, (struct sockaddr *)&sa, &len);
		if (unlikely(err < 0)) {
			error("getsockname() failed. errno = %d says '%s'.", errno, strerror(errno));
			return -1;
		}

		if (unlikely(len != sizeof(sa))) {
			error("Size mismatch.");
			return -1;
		}

		snprintf(addr, sizeof(addr), "%s", inet_ntoa(sa.sin_addr));
		snprintf(port, sizeof(port), "%d", (int )ntohs(sa.sin_port));
	}

	n = (spawn->nhosts + devel_tree_width - 1)/devel_tree_width;

	for (i = 0, j = 1; i < spawn->nhosts; i += n, ++j) {
		/* FIXME Threaded spawning! Take devel_fanout into account. */
		/* At the same time, while spawning we need to accept connections! */

		snprintf(argv1, sizeof(argv1), "%d", 0);		/* my participant id */
		snprintf(argv2, sizeof(argv2), "%d", spawn->nhosts);	/* size */
		snprintf(argv3, sizeof(argv3), "%d", i + 1);		/* their participant id */

		char *const argw[] = {SPAWN_EXE_OTHER,
		                      addr, port,
		                      argv1, argv2, argv3,
		                      NULL};
		err = spawn->exec->ops->exec(spawn->exec, "localhost", argw);

		/*
		 * Wait for connections. Handle the case where a host does not connect back in a sufficient
		 * amount of time or where the exec fails().
		 */

		while (1) {
			newfd = atomic_read(spawn->tree.newfd);
			if (-1 != newfd) {
				tmp = atomic_cmpxchg(spawn->tree.newfd, newfd, -1);
				if (unlikely(newfd != tmp)) {
					error("Detected unexpected write to tree.newfd.");
					die();
				}

				/* Temporarily disable the communication thread. Otherwise it
				 * happen that we wait for seconds before acquiring the lock.
				 * Getting the lock in this situation is a bit overcautious.
				 */
				err = comm_stop_processing(&spawn->comm);
				if (unlikely(err)) {
					error("Failed to temporarily stop the communication thread.");
				}

				err = network_lock_acquire(&spawn->tree);
				if (unlikely(err))
					die();

				log("Got you: %d", newfd);

				/* TODO Keep the ports in a local array. Insert them at a later
				 *      point all at once!
				 */

				err = network_lock_release(&spawn->tree);
				if (unlikely(err))
					die();

				err = comm_resume_processing(&spawn->comm);
				if (unlikely(err)) {
					error("Failed to resume the communication thread.");
					die();	/* Pretty much impossible that this happen. If it does
						 * we are screwed though. */
				}

				break;
			}
		}

#if 0
		ll addrlen;
		int sock = do_accept(spawn->tree.listenfd, NULL, NULL);

		log("sock = %d", sock);

		struct message_header header;
		struct message_request_join msg;
		struct buffer buffer;

		buffer_ctor(&buffer, spawn->alloc, 1024);	/* assert(struct(message_header) < 1024)!!! */

		buffer.size = read(sock, buffer.buf + buffer.pos, sizeof(header));
		log("buffer.size = %d %s", buffer.size, strerror(errno));

		int foo = unpack_message_header(&buffer, &header);
		log(" foo = %d");

		log("%d %d %d", (int )header.src, (int )header.dst, (int )header.payload);

		buffer.size += read(sock, buffer.buf + buffer.pos, header.payload);

		foo = unpack_message_payload(&buffer, &header, spawn->alloc, (void *)&msg);

		log(" ###### %d ######", msg.pid);

		buffer_dtor(&buffer);
#endif

		/* configure lft for hosts in the range ... */
	}
/* ************************************************************ */

	err = spawn_comm_halt(spawn);
	if (unlikely(err)) {
		error("Failed to stop the communication module.");
		return err;
	}

	return 0;
}

static int _main_on_other(struct spawn *spawn, int argc, char **argv)
{
	int err, tmp;
	struct sockaddr_in sa;
	struct _args_other args;

	/*
	 * Done before daemonize() such that the exec plugin can return
	 * a proper error code.
	 */
	err = _parse_argv_on_other(argc, argv, &args);
	if (unlikely(err))
		return err;

	err = daemonize();
	if (unlikely(err)) {
		error("daemonize() failed with error %d.", err);
		return err;	/* Failure to daemonize may result in
				 * deadlocks. */
	}

	err = spawn_setup_on_other(spawn, args.size, args.here);
	if (unlikely(err)) {
		error("Failed to setup spawn instance.");
		return err;
	}

	/* FIXME In a real setup we will not know which address to bind to
	 *       This information needs to be either passed via the command line
	 *       (inflexible) or received as part of the RESPONSE_JOIN message.
	 *       In the latter case we would need to move the bind operation to
	 *	 a later stage and make sure that the communication thread can
	 *	 properly handle the case where the listenfd is -1.
	 *	 Which is complicated! It probably is easier to connect listenfd
	 *       either to /dev/null or bind it to the loopback interface and
         *       modify it later!
	 */
	_localaddr(&sa);

	err = spawn_bind_listenfd(spawn, (struct sockaddr *)&sa, sizeof(sa));
	if (unlikely(err)) {
		error("Failed to bind the listenfd.");
		return err;
	}

	err = _connect_to_father(spawn, &args.sa);
	if (unlikely(err)) {
		error("Failed to connect to father process.");
		return err;
	}

	err = spawn_comm_start(spawn);
	if (unlikely(err)) {
		error("Failed to start the communication module.");
		return err;
	}

	err = let_the_watchog_loose(60);	/* FIXME Make this parameter configurable.
						 *	 But if it is configurable we cannot
						 *       start the watchdog here but first
						 *       need to receive the options from
						 *       the father.
						 */
	if (unlikely(err)) {
		error("Failed to start the watchdog thread.");
		/* continue anyway. */
	}

	err = _join(spawn);
	if (unlikely(err)) {
		error("Failed to join the network.");
		goto fail;
	}

	while (1) {
		/* FIXME Process incoming messages. */

		struct timespec ts;

		ts.tv_sec  = 1;
		ts.tv_nsec = 0;
		nanosleep(&ts, NULL);
	}

	err = spawn_comm_halt(spawn);
	if (unlikely(err)) {
		error("Failed to halt the communication module.");
		return err;
	}

	return 0;

fail:
	tmp = spawn_comm_halt(spawn);
	if (unlikely(tmp))
		error("Failed to halt the communication module.");

	return err;
}

/*
 * Fill sa with the loopback address and any port.
 */
static int _localaddr(struct sockaddr_in *sa)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_addr.s_addr = htonl(IP4ADDR(127,0,0,1));

	return 0;
}

static int _parse_argv_on_other(int argc, char **argv, struct _args_other *args)
{
	int err, p;

	if (unlikely(6 != argc)) {
		error("Incorrect number of arguments.");
		return -EINVAL;
	}

	memset(&args->sa, 0, sizeof(args->sa));
	args->sa.sin_family = AF_INET;

	err = inet_aton(argv[1], &args->sa.sin_addr);
	if (0 == err) {
		error("inet_aton() failed.");
		return -EINVAL;
	}

	p = strtol(argv[2], NULL, 10);
	if (0 == p) {	/* Zero is not a valid port */
		error("Invalid port specified on command line.");
		return -EINVAL;
	}
	args->sa.sin_port = htons((short )p);

	args->father = strtol(argv[3], NULL, 10);
	args->size   = strtol(argv[4], NULL, 10);
	args->here   = strtol(argv[5], NULL, 10);

	if (0 == args->size) {
		error("Invalid size specified on command line.");
		return -EINVAL;
	}
	if (0 == args->here) {
		error("Invalid id specified on command line.");
		return -EINVAL;
	}

	return 0;
}


static int _connect_to_father(struct spawn *spawn, struct sockaddr_in *sa)
{
	int err;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(fd < 0)) {
		error("socket() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	err = do_connect(fd, (struct sockaddr *)sa, sizeof(*sa));
	if (unlikely(err))	/* Let do_connect() report the error. */
		goto fail;

	err = network_add_ports(&spawn->tree, &fd, 1);
	if (unlikely(err)) {
		error("network_add_ports() failed with error %d.", err);
		goto fail;
	}

	err = network_initialize_lft(&spawn->tree, 0);
	if (unlikely(err)) {
		error("network_initialize_lft() failed with error %d.", err);
		goto fail;
	}

	return 0;

fail:
	assert(err);

	close(fd);

	return err;
}

static int _join(struct spawn *spawn)
{
/* ************************************************************ */
	/* FIXME Send REQUEST_JOIN. */
	/* FIXME Recv RESPONSE_JOIN. */

	/*
	struct message_header header;
	struct message_request_join msg;
	struct buffer buffer;

	memset(&header, 0, sizeof(header));

	header.src   = strtol(argv[4], NULL, 10);
	header.dst   = strtol(argv[3], NULL, 10);
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = REQUEST_JOIN;

	msg.pid = getpid();

	buffer_ctor(&buffer, spawn->alloc, 1024);

	pack_message(&buffer, &header, (void *)&msg);

	write(sock, buffer.buf, buffer.size);
	buffer_dtor(&buffer);
	*/
/* ************************************************************ */

	return -ENOTIMPL;
}

