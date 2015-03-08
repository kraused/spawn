
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
#include <fcntl.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "helper.h"
#include "spawn.h"
#include "watchdog.h"
#include "loop.h"
#include "job.h"
#include "protocol.h"


/*
 * Some information is cumbersome to transfer via the wire since
 * they are necessary to have a properly prepared communication
 * module.
 */
struct _args_other
{
	/* Address of the parent to connect to. Obviously needs
	 * to be passed via command line (or environment).
	 */
	struct sockaddr_in	sa;
	/* Network identifier of the parent. Required for the
	 * header of the MESSAGE_TYPE_REQUEST_JOIN message.
	 */
	int			parent;
	/* Size of the network. Required in order to setup the
	 * LFT.
	 */
	int			size;
	/* Network identifier of this process. Required for the
	 * header of the MESSAGE_TYPE_REQUEST_JOIN message.
	 */
	int			here;
};

static int _main_on_local(int argc, char **argv);
static int _main_on_other(int argc, char **argv);
static int _run_loop(struct spawn *spawn);
static int _parse_argv_on_other(int argc, char **argv, struct _args_other *args);
static int _redirect_stdio();
static int _join(struct alloc *alloc, struct _args_other *args, int *fd,
                 struct optpool **opts);
static int _send_join_request(struct alloc *alloc, int parent, int here, int fd);
static int _recv_join_response(struct alloc *alloc, int fd,
                               struct optpool **opts);
static int _connect_to_parent(struct sockaddr_in *sa, int *fd);
static struct optpool *_alloc_and_fill_optpool(struct alloc *alloc,
                                               const char *file, char **argv);
static int _check_important_options(struct optpool *opts);
static int _ignore_sigpipe();


int main(int argc, char **argv)
{
	int n, m;
	int err;

	/* Handle broken pipes in the caller of write()/read().
	 */
	_ignore_sigpipe();
	{
		struct sigaction act, oact;

		act.sa_handler = SIG_IGN;
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_RESTART;

		sigaction(SIGPIPE, &act, &oact);
	}

	/* Do as little as possible at this point. _main_on_other() will
	 * call daemonize() and it is easier to setup everything after
	 * this point.
	 * In an earlier version of this code we called spawn_ctor() at
	 * this point. Since spawn_ctor() created the communication thread
	 * the fork() in daemonize() destroyed everything.
	 */

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
		err = _main_on_other(argc, argv);
	else
		err = _main_on_local(argc, argv);

	if (unlikely(err)) {
		error("main function failed with exit code %d.", err);
		return err;
	}

	return err;
}


static int _main_on_local(int argc, char **argv)
{
	int err, tmp;
	struct alloc *alloc;
	struct spawn spawn;
	struct job *job;
	const char *path;
	struct optpool *opts;

	alloc = libc_allocator_with_debugging();

	opts = _alloc_and_fill_optpool(alloc, SPAWN_CONFIG_DEFAULT, argv);
	if (unlikely(!opts))
		return -ESOMEFAULT;

	/* It would be annoying to spawn thousands of processes and only notice that
	 * we mistyped an option once the tree is completely set up. Instead we
	 * check some important options (in particular those for which no default value
	 * is read) in advance.
	 */
	err = _check_important_options(opts);
	if (unlikely(err))
		return err;

	err = spawn_ctor(&spawn, alloc, opts, -1, 0);
	if (unlikely(err)) {
		error("struct spawn constructor failed with exit code %d.", err);
		return err;
	}

	err = alloc_job_build_tree(alloc, &spawn, spawn.nhosts,
	                           NULL, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_build_tree", err);
		goto fail;
	}

	list_insert_before(&spawn.jobs, &job->list);

	path = optpool_find_by_key(spawn.opts, "ExecPlugin");
	if (unlikely(!path)) {
		error("Missing 'ExecPlugin' option.");
		return -EINVAL;
	}

	err = spawn_setup_worker_pool(&spawn, path);
	if (unlikely(err))
		return err;

	err = _run_loop(&spawn);
	if (unlikely(err))
		return err;

	err = spawn_dtor(&spawn);
	if (unlikely(err)) {
		error("struct spawn destructor failed with exit code %d.", err);
		return err;
	}

	return 0;

fail:
	tmp = spawn_comm_halt(&spawn);
	if (unlikely(tmp))
		error("Failed to halt the communication module.");

	return err;
}

static int _main_on_other(int argc, char **argv)
{
	int err;
	struct alloc *alloc;
	struct spawn spawn;
	struct _args_other args;
	struct optpool *opts;
	int fd, timeout;

	/*
	 * Done before daemonize() such that the exec plugin can return
	 * a proper error code.
	 */
	err = _parse_argv_on_other(argc, argv, &args);
	if (unlikely(err))
		return err;

	err = _redirect_stdio();
	if (unlikely(err))
		die();

	/* FIXME What are error(), debug(), ... doing when stdout and stderr
	 *       are closed?
	 */

	err = daemonize();
	if (unlikely(err)) {
		fcallerror("daemonize", err);
		return err;	/* Failure to daemonize may result in
				 * deadlocks. */
	}

	alloc = libc_allocator_with_debugging();

	err = _join(alloc, &args, &fd, &opts);
	if (unlikely(err)) {
		error("Failed to join the network.");
		return err;
	}

	err = optpool_find_by_key_as_int(opts, "WatchdogTimeout", &timeout);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		timeout = 10;
	}

	err = let_the_watchog_loose(timeout);
	if (unlikely(err)) {
		error("Failed to start the watchdog thread.");
		/* continue anyway. */
	}

	err = spawn_ctor(&spawn, alloc, opts, args.parent, args.here);
	if (unlikely(err)) {
		error("struct spawn constructor failed with exit code %d.", err);
		return err;
	}

	err = network_add_ports(&spawn.tree, &fd, 1);
	if (unlikely(err)) {
		fcallerror("network_add_ports", err);
		return err;
	}

	err = network_initialize_lft(&spawn.tree, 0);
	if (unlikely(err)) {
		fcallerror("network_initialize_lft", err);
		return err;
	}

	err = _run_loop(&spawn);
	if (unlikely(err))
		return err;

	err = spawn_dtor(&spawn);
	if (unlikely(err)) {
		error("struct spawn destructor failed with exit code %d.", err);
		return err;
	}

	return 0;
}

static int _run_loop(struct spawn *spawn)
{
	int err;

	err = spawn_comm_start(spawn);
	if (unlikely(err)) {
		error("Failed to start the communication module.");
		return err;
	}

	err = loop(spawn);
	if (unlikely(err))
		fcallerror("loop", err);	/* Continue anyway with a proper shutdown. */

	err = spawn_comm_halt(spawn);
	if (unlikely(err)) {
		error("Failed to halt the communication module.");
		return err;
	}

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

	args->parent = strtol(argv[3], NULL, 10);
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

static int _redirect_stdio()
{
	int i, fd;
	char buf[64];

	snprintf(buf, sizeof(buf), "/tmp/spawn-%04d.log", (int )getpid());

	for (i = 1; i < 1024; ++i)
		close(i);

	fd = open(buf, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (unlikely(1 != fd))
		die();	/* No way to write an error message at this point.
			 */

	/* FIXME Handle dup() error */
	dup(1);

	return 0;
}

/*
 * TODO Introduce a timeout for _join(). The watchdog is not yet running
 *      and _join() may block indefinitely so that processes may survive
 *      in case of a failure.
 */
static int _join(struct alloc *alloc, struct _args_other *args,
                 int *fd, struct optpool **opts)
{
	int err;

	err = _connect_to_parent(&args->sa, fd);
	if (unlikely(err)) {
		error("Failed to connect to parent process.");
		return err;
	}

	err = _send_join_request(alloc, args->parent, args->here, *fd);
	if (unlikely(err)) {
		fcallerror("_join_send_request", err);
		goto fail;
	}

	err = _recv_join_response(alloc, *fd, opts);
	if (unlikely(err)) {
		fcallerror("_join_recv_response", err);
		goto fail;
	}

	debug("Successfully joined.");

	return 0;

fail:
	do_close(*fd);

	return err;
}

/*
 * Connect to the parent in the tree.
 */
static int _connect_to_parent(struct sockaddr_in *sa, int *fd)
{
	int err;

	*fd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely((*fd) < 0)) {
		error("socket() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	err = do_connect(*fd, (struct sockaddr *)sa, sizeof(*sa));
	if (unlikely(err))	/* Let do_connect() report the error. */
		goto fail;

	debug("Connection to parent process established.");

	return 0;

fail:
	do_close(*fd);

	return err;
}

static int _send_join_request(struct alloc *alloc, int parent, int here, int fd)
{
	int tmp, err;
	struct message_header       header;
	struct message_request_join msg;
	struct buffer buf;

	memset(&header, 0, sizeof(header));
	memset(&msg   , 0, sizeof(msg));

	header.src   = here;
	header.dst   = parent;
	header.flags = MESSAGE_FLAG_UCAST;
	header.type  = MESSAGE_TYPE_REQUEST_JOIN;

	msg.pid = getpid();

	err = sockaddr(fd, &msg.ip, &msg.portnum);
	if (unlikely(err))
		return err;

	/* Multiply by two to make sure that pack_message() does
	 * not lead to a reallocation.
	 */
	err = buffer_ctor(&buf, alloc, 2*(sizeof(header) + sizeof(msg)));
	if (unlikely(err)) {
		fcallerror("buffer_ctor", err);
		return err;
	}

	err = pack_message(&buf, &header, &msg);
	if (unlikely(err)) {
		fcallerror("pack_message", err);
		goto fail;
	}

	err = do_write_loop(fd, buf.buf, buf.size);
	if (unlikely(err))
		goto fail;

	err = buffer_dtor(&buf);
	if (unlikely(err)) {
		fcallerror("buffer_dtor", err);
		return err;
	}

	return 0;

fail:
	tmp = buffer_dtor(&buf);
	if (unlikely(tmp))
		fcallerror("buffer_dtor", tmp);

	return err;
}

static int _recv_join_response(struct alloc *alloc, int fd,
                               struct optpool **opts)
{
	int err, tmp;
	struct message_header        header;
	struct message_response_join msg;
	struct buffer buf;

	err = buffer_ctor(&buf, alloc, sizeof(header));
	if (unlikely(err)) {
		fcallerror("buffer_ctor", err);
		return err;
	}

	err = buffer_resize(&buf, sizeof(header));
	if (unlikely(err)) {
		fcallerror("buffer_resize", err);
		goto fail;
	}

	while (!buffer_pos_equal_size(&buf)) {
		err = buffer_read(&buf, fd);
		if (unlikely(err)) {
			fcallerror("buffer_read", err);
			goto fail;
		}
	}

	err = secretly_copy_header(&buf, &header);
	if (unlikely(err)) {
		fcallerror("secretly_copy_message_header", err);
		goto fail;
	}

	err = buffer_resize(&buf, sizeof(header) + header.payload);
	if (unlikely(err)) {
		fcallerror("buffer_resize", err);
		goto fail;
	}

	while (!buffer_pos_equal_size(&buf)) {
		err = buffer_read(&buf, fd);
		if (unlikely(err)) {
			fcallerror("buffer_read", err);
			goto fail;
		}
	}

	err = buffer_seek(&buf, sizeof(header));
	if (unlikely(err)) {
		fcallerror("buffer_seek", err);
		goto fail;
	}

	err = unpack_message_payload(&buf, &header, alloc, &msg);
	if (unlikely(err)) {
		fcallerror("unpack_message_payload", err);
		goto fail;
	}

	*opts = msg.opts;

	err = buffer_dtor(&buf);
	if (unlikely(err)) {
		fcallerror("buffer_dtor", err);
		return err;
	}

	return 0;

fail:
	tmp = buffer_dtor(&buf);
	if (unlikely(tmp))
		fcallerror("buffer_dtor", tmp);

	return err;
}

static struct optpool *_alloc_and_fill_optpool(struct alloc *alloc,
                                               const char *file, char **argv)
{
	int err;
	struct optpool *opts;
	FILE *fp;

	err = ZALLOC(alloc, (void **)&opts, 1, sizeof(struct optpool), "opts");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return NULL;
	}

	err = optpool_ctor(opts, alloc);
	if (unlikely(err)) {
		fcallerror("optpool_ctor", err);
		goto fail;
	}

	fp = fopen(file, "r");
	if (unlikely(!fp)) {
		error("Failed to open configuration file '%s'.", file);
		goto fail;
	}

	err = optpool_parse_file(opts, fp);
	if (unlikely(err)) {
		error("Failed to parse the configuration file '%s' " \
		      "(optpool_parse_cmdline_args failed with error %d)",
		      file, err);
		fclose(fp);
		goto fail;
	}

	fclose(fp);

	err = optpool_parse_cmdline_args(opts, argv);
	if (unlikely(err)) {
		error("Failed to parse the command line arguments " \
		      "(optpool_parse_cmdline_args failed with error %d)", err);
		goto fail;
	}

	return opts;

fail:
	err = ZFREE(alloc, (void **)&opts, 1, sizeof(struct optpool), "");
	if (unlikely(err))
		fcallerror("ZFREE", err);

	return NULL;
}

static int _check_important_options(struct optpool *opts)
{
	const char *p;

	p = optpool_find_by_key(opts, "TaskPlugin");
	if (unlikely(!p)) {
		error("Missing 'TaskPlugin' option.");
		return -EINVAL;
	}

	p = optpool_find_by_key(opts, "Hosts");
	if (unlikely(!p)) {
		error("Missing 'Hosts' option.");
		return -EINVAL;
	}

	return 0;
}

static int _ignore_sigpipe()
{
	struct sigaction act, oact;

	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	sigaction(SIGPIPE, &act, &oact);

	return 0;
}

