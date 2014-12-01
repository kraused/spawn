
#ifndef SPAWN_SPAWN_H_INCLUDED
#define SPAWN_SPAWN_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "comm.h"
#include "network.h"
#include "pack.h"

struct sockaddr;

struct alloc;
struct message_header;
struct exec_plugin;


/*
 * Record of a process on a (potentially remote) host.
 */
struct process
{
	int	host;
	ll	pid;
	int	port;	/* Port through which the process is
		         * reachable. */
};

/*
 * Main data structure that stores everything required by the program.
 */
struct spawn
{
	struct alloc		*alloc;

	/* Number of hosts. This number includes the host on which the
	 * spawn program itself is running.
	 */
	int			nhosts;
	/* Names of the hosts in the network. This is non-NULL only on
	 * the root of the tree.
	 */
	const char		**hosts;

	struct network		tree;
	struct comm		comm;
	struct buffer_pool	bufpool;

	/* Direct child processes. Unless the tree width is large enough
	 * this will only be subset of all processes spawned.
	 */
	int			nprocs;
	struct process		*procs;

	struct exec_plugin	*exec;
};

/*
 * Initialize a spawn instance.
 */
int spawn_ctor(struct spawn *self, struct alloc *alloc);

/*
 * Free spawn resources.
 */
int spawn_dtor(struct spawn *self);

/*
 * Setup the spawn instance on the local host.
 */
int spawn_setup_on_local(struct spawn *self, int nhosts,
                         const char **hosts, int treewidth);

/*
 * Setup the spawn instance on all other hosts.
 */
int spawn_setup_on_other(struct spawn *self, int nhosts,
                         int here);

/*
 * Load the exec plugin.
 */
int spawn_load_exec_plugin(struct spawn *self, const char *name);

/*
 * Bind the listenfd to the given address.
 */
int spawn_bind_listenfd(struct spawn *self, struct sockaddr *addr, ull addrlen);

/*
 * Start the communication module. The function begins listening
 * for incoming connections and starts the communication thread.
 */
int spawn_comm_start(struct spawn *self);

/*
 * Halt the communication module. Close the listenfd.
 */
int spawn_comm_halt(struct spawn *self);

/*
 * Send a message.
 */
int spawn_send_message(struct spawn *self, struct message_header *header, void *msg);

#endif

