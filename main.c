
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

#include "devel.h"


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
static int _localaddr(struct sockaddr_in *sa);
static int _parse_argv_on_other(int argc, char **argv, struct _args_other *args);
static int _redirect_stdio();
static int _connect_to_parent(struct spawn *spawn, struct sockaddr_in *sa);
static struct optpool *_alloc_and_fill_optpool(struct alloc *alloc,
                                               const char *file, char **argv);
static int _check_important_options(struct optpool *opts);


int main(int argc, char **argv)
{
	int n, m;
	int err;

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
	struct sockaddr_in sa;
	struct job *job;
	const char *path;
	struct optpool *opts;

	alloc = libc_allocator_with_debugging();

	err = spawn_ctor(&spawn, alloc);
	if (unlikely(err)) {
		error("struct spawn constructor failed with exit code %d.", err);
		return err;
	}

	opts = _alloc_and_fill_optpool(spawn.alloc, SPAWN_CONFIG_DEFAULT, argv);
	if (unlikely(err))
		return err;

	/* It would be annoying to spawn thousands of processes and only notice that
	 * we mistyped an option once the tree is completely set up. Instead we
	 * check some important options (in particular those for which no default value
	 * is read) in advance.
	 */
	err = _check_important_options(opts);
	if (unlikely(err))
		return err;

	err = spawn_setup_on_local(&spawn, opts, devel_nhosts, devel_hostlist, devel_tree_width);
	if (unlikely(err)) {
		error("Failed to setup spawn instance.");
		return err;
	}

	path = optpool_find_by_key(spawn.opts, "ExecPlugin");
	if (unlikely(!path)) {
		error("Missing 'ExecPlugin' option.");
		return -EINVAL;
	}

	err = spawn_setup_worker_pool(&spawn, path);
	if (unlikely(err))
		return err;

	_localaddr(&sa);

	err = spawn_bind_listenfd(&spawn, (struct sockaddr *)&sa, sizeof(sa));
	if (unlikely(err)) {
		error("Failed to bind the listenfd.");
		return err;
	}

	err = spawn_comm_start(&spawn);
	if (unlikely(err)) {
		error("Failed to start the communication module.");
		return err;
	}

	err = alloc_job_build_tree(alloc, &spawn, devel_nhosts, devel_hostlist, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_build_tree", err);
		goto fail;
	}

	list_insert_before(&spawn.jobs, &job->list);

	err = loop(&spawn);
	if (unlikely(err))
		fcallerror("loop", err);	/* Continue with the shutdown */

	err = spawn_comm_halt(&spawn);
	if (unlikely(err)) {
		error("Failed to stop the communication module.");
		return err;
	}

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
	int err, tmp;
	struct alloc *alloc;
	struct spawn spawn;
	struct sockaddr_in sa;
	struct _args_other args;
	struct job *job;

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

	err = spawn_ctor(&spawn, alloc);
	if (unlikely(err)) {
		error("struct spawn constructor failed with exit code %d.", err);
		return err;
	}

	err = spawn_setup_on_other(&spawn, args.size, args.parent, args.here);
	if (unlikely(err)) {
		error("Failed to setup spawn instance.");
		return err;
	}

	/* FIXME In a real setup we will not know which address to bind to
	 *       This information needs to be either passed via the command line
	 *       (inflexible) or received as part of the MESSAGE_TYPE_RESPONSE_JOIN
	 * 	 message. In the latter case we would need to move the bind
	 *	 operation to a later stage and make sure that the communication
	 *	 thread can properly handle the case where the listenfd is -1.
	 *	 Which is complicated! It probably is easier to connect listenfd
	 *       either to /dev/null or bind it to the loopback interface and
	 *       modify it later!
	 */
	_localaddr(&sa);

	err = spawn_bind_listenfd(&spawn, (struct sockaddr *)&sa, sizeof(sa));
	if (unlikely(err)) {
		error("Failed to bind the listenfd.");
		return err;
	}

	err = _connect_to_parent(&spawn, &args.sa);
	if (unlikely(err)) {
		error("Failed to connect to parent process.");
		return err;
	}

	err = spawn_comm_start(&spawn);
	if (unlikely(err)) {
		error("Failed to start the communication module.");
		return err;
	}

	err = let_the_watchog_loose(60);	/* FIXME Make this parameter configurable.
						 *	 But if it is configurable we cannot
						 *       start the watchdog here but first
						 *       need to receive the options from
						 *       the parent.
						 *	 We could also start with a hardcoded
						 *       default and then change it afterwards!
						 */
	if (unlikely(err)) {
		error("Failed to start the watchdog thread.");
		/* continue anyway. */
	}

	err = alloc_job_join(alloc, args.parent, &job);
	if (unlikely(err)) {
		fcallerror("alloc_job_join", err);
		goto fail;
	}

	list_insert_before(&spawn.jobs, &job->list);

	err = loop(&spawn);
	if (unlikely(err))
		fcallerror("loop", err);	/* Continue anyway with a proper shutdown. */

	err = spawn_comm_halt(&spawn);
	if (unlikely(err)) {
		error("Failed to halt the communication module.");
		return err;
	}

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
 * Connect to the parent in the tree. This is done outside of the main loop.
 */
static int _connect_to_parent(struct spawn *spawn, struct sockaddr_in *sa)
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
		fcallerror("network_add_ports", err);
		goto fail;
	}

	err = network_initialize_lft(&spawn->tree, 0);
	if (unlikely(err)) {
		fcallerror("network_initialize_lft", err);
		goto fail;
	}

	return 0;

fail:
	assert(err);

	close(fd);

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

	return 0;
}

