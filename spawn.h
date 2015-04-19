
#ifndef SPAWN_SPAWN_H_INCLUDED
#define SPAWN_SPAWN_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "hostinfo.h"
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
 * Record of a remote (child) process.
 */
struct process
{
	int	id;	/* Network participant id. Subtract one to get the
			 * offset into the hosts array.
			 */
	ll	pid;
	/* Remote address information.
	 */
	struct {
		ui32	ip;
		ui32	portnum;
	}	addr;
	int	port;	/* Port through which the child process is
			 * reachable.
			 */
};

/*
 * Main data structure that stores everything required by the program.
 */
struct spawn
{
	struct alloc		*alloc;

	struct hostinfo		hostinfo;

	struct optpool		*opts;

	/* Number of hosts. This number includes the host on which the
	 * spawn program itself is running.
	 */
	int			nhosts;
	/* Names of the hosts in the network.
	 */
	char			**hosts;

	/* Participant id of the parent.
	 */
	int			parent;

	struct network		tree;

	int			nprocs;
	struct process		*procs;

	struct comm		comm;
	struct buffer_pool	bufpool;

	/* List of jobs to be executed. See loop() in loop.c.
	 */
	struct list		jobs;

	struct exec_plugin	*exec;
	struct exec_worker_pool	*wkpool;

	/* Buffered stdout and stderr content on remote processes.
	 */
	struct msgbuf		*bout;
	struct msgbuf		*berr;
};

/*
 * Initialize a spawn instance.
 */
int spawn_ctor(struct spawn *self, struct alloc *alloc, struct optpool *opts,
               int parent, int here);

/*
 * Free spawn resources.
 */
int spawn_dtor(struct spawn *self);

/*
 * Load the exec plugin and setup the worker pool. The workers are not yet
 * active. path is the filesystem path to the DSO file containing the exec
 * plugin.
 */
int spawn_setup_worker_pool(struct spawn *self, const char *path);

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
 * Reserve a virtual channel for a plugin.
 */
int spawn_comm_resv_channel(struct spawn *self, ui16 *channel);

/*
 * Send a message.
 */
int spawn_send_message(struct spawn *self, struct message_header *header, void *msg);

#endif

