
#ifndef SPAWN_NETWORK_H_INCLUDED
#define SPAWN_NETWORK_H_INCLUDED 1

#include "ints.h"
#include "thread.h"

struct alloc;

/*
 * Network data structure.
 */
struct network
{
	struct alloc	*alloc;

	/* The actual network size is restricted to 65536 participants
	 * since we use an unsigned integer of width 16 in the protocol.
	 */
	si32		size;
	si32		here;	/* Participant identifier */

	/* Linear forwarding table for network routing. The lft maps the
	 * identifier of each participant to the port to which packets
	 * should be passed.
	 */
	si32		*lft;

	/* Each port is a socket. nports is equal to the tree width plus one
	 * and equals the storage size of ports.
	 * The first port is always the connection down the tree.
	 */
	int		nports;
	int		*ports;

	/* Socket used to listen for connections from children in the tree.
	 * When running in a single broadcast domain such as a homogeneous
	 * cluster usually only one file descriptor is needed.
	 */
#define NETWORK_MAX_LISTENFDS	8
	int		nlistenfds;
	int		listenfds[NETWORK_MAX_LISTENFDS];

	/* New connection. The communication thread will accept a new connection
	 * if newfd is not equal to -1. Access to newfd should be done by atomic
	 * reads and writes with or without holding the lock.
	 *
	 * FIXME Maybe replace newfd by an array to avoid a bottleneck?
	 */
	int		newfd;

	/* Lock for access to members of the network structure. */
	struct lock	lock;
};

int network_ctor(struct network *self, struct alloc *alloc);
int network_dtor(struct network *self);

/*
 * Acquire and release the lock protecting struct network members.
 */
int network_lock_acquire(struct network *self);
int network_lock_release(struct network *self);

/*
 * Resize the network.
 */
int network_resize(struct network *self, int size);

/*
 * Add new listening sockets.
 */
int network_add_listenfds(struct network *self, int *fds, int nfds);

/*
 * Add some new ports to the network. The LFT is left unchanged.
 * Make sure to hold the lock when calling this function.
 */
int network_add_ports(struct network *self, int *fds, int nfds);

/*
 * Initialize the LFT such that messages to all network participants
 * are routed through the given port.
 *
 * FIXME The use of network_initialize_lft() has the disadvantage that
 *       we will have (seemingly) valid routing entries for potentially
 *       non-existing hosts.
 */
int network_initialize_lft(struct network *self, int port);

/*
 * Modify the LFT such that all packages for the specified ids are routed
 * through the given port.
 *
 * Make sure to hold the lock when calling this function.
 */
int network_modify_lft(struct network *self, int port, si32 *ids, si32 nids);

/*
 * Print the LFT for debugging purposes
 */
int network_debug_print_lft(struct network *self);

#endif

