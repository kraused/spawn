
#ifndef SPAWN_SPAWN_H_INCLUDED
#define SPAWN_SPAWN_H_INCLUDED 1

#include "ints.h"

struct sockaddr;

struct alloc;
struct exec_plugin;


/*
 * Network data structure.
 */
struct network
{
	/* The actual network size is restricted to 65536 participants
	 * since we use an unsigned integer of width 16 in the protocol.
	 */
	si32	size;
	si32	here;	/* Participant identifier */
	si32	*lft;

	/*
	 * Each port is a socket
	 */
	int	nports;
	int	*ports;
};

/*
 * Record of a process on a (potentially remote) host.
 */
struct process
{
	int		host;
	long long	pid;
	int		port;	/* Port through which the process is
			         * reachable. */
};

/*
 * Main data structure that stores everything required by the program.
 */
struct spawn
{
	struct alloc	*alloc;

	int		nhosts;
	const char	**hosts;
	struct network	tree;

	/* Direct child processes. Unless the tree width is large enough
	 * this will only be subset of all processes spawned.
	 */
	int		nprocs;
	struct process	*procs;

	/* Socket used to listen to connections from children in the
	 * tree. Managed seperately from the struct network since the
	 * socket is closed as soon as the network is set up.
	 */
	int	listenfd;

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
 * Start listening on the listenfd socket.
 */
int spawn_start_listen(struct spawn *self, struct sockaddr *addr,
                       unsigned long long addrlen, int backlog);

/*
 * Close listenfd.
 */
int spawn_close_listen(struct spawn *self);

#endif

