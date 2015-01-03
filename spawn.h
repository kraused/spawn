
#ifndef SPAWN_SPAWN_H_INCLUDED
#define SPAWN_SPAWN_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "comm.h"
#include "network.h"
#include "pack.h"
#include "list.h"
#include "worker.h"
#include "options.h"

struct sockaddr;

struct alloc;
struct message_header;
struct exec_plugin;


/*
 * Main data structure that stores everything required by the program.
 */
struct spawn
{
	struct alloc		*alloc;

	struct optpool		opts;

	/* Number of hosts. This number includes the host on which the
	 * spawn program itself is running.
	 */
	int			nhosts;
	/* Names of the hosts in the network. This is non-NULL only in
	 * the context of the master process (root of the tree).
	 */
	const char		**hosts;

	/* Participant id of the parent.
	 */
	int			parent;

	struct network		tree;

	struct comm		comm;
	struct buffer_pool	bufpool;

	/* List of jobs to be executed. See loop() in loop.c.
	 */
	struct list		jobs;

	/* List of tasks.
	 */
	struct list		tasks;

	struct exec_plugin	*exec;
	struct exec_worker_pool	*wkpool;
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
                         int parent, int here);

/*
 * Load the exec plugin and setup the worker pool. The workers are not yet
 * active. path is the filesystem path to the DSO file containing the exec
 * plugin.
 */
int spawn_setup_worker_pool(struct spawn *self, const char *path);

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
 * Flush the communication queue.
 */
int spawn_comm_flush(struct spawn *self);

/*
 * Send a message.
 */
int spawn_send_message(struct spawn *self, struct message_header *header, void *msg);

#endif

