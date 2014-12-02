
#ifndef SPAWN_COMM_H_INCLUDED
#define SPAWN_COMM_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "queue.h"

struct alloc;
struct buffer;

/* This program uses an asynchronous messaging paradigm. A separate
 * communication thread is responsible for write()s and read()s to/from
 * the sockets.
 *
 * FIXME The current interface does not provide the possibility to return
 *       exit codes. Instead the communication thread will try indefinitely
 *       to send a message. This means that we cannot handle broken
 *       connections.
 *
 */

/*
 * Lock-based queue for buffers. The queues are the main interface for
 * interaction with the communication thread.
 */
struct comm_queue
{
	struct queue		queue;
	struct lock		lock;
};

/*
 * Communication module.
 */
struct comm
{
	struct alloc		*alloc;

	/* Network datastructure required for routing. */
	struct network		*net;

	/* Buffer pool. Buffers that have been processed will be
	 * returned to the pool. Receive buffers for incoming
	 * messages will be taken from the buffer.
	 */
	struct buffer_pool	*bufpool;

	/* Queues for send requests and incoming messages. */
	struct comm_queue	sendq;
	struct comm_queue	recvq;

	/* Condition variable that threads can block on to be notified
	 * about the availability of new buffers.
	 */
	struct cond_var		cond;

	/* Set to one to force the temporary shutdown of the
	 * communication thread. Set it to two in order to shutdown
	 * the communication thread completely.
	 */
	int			stop;

	/* Thread handle for the communication thread
	 */
	struct thread		thread;

	/* For poll() syscall.  Size is (1 + net->nports). */
	int			npollfds;
	struct pollfd		*pollfds;

	/* Buffers for currently ongoing send and receive operations
	 * ordered according to the port. Size is net->nports.
	 */
	struct buffer		**recvb;
	struct buffer		**sendb;

	/* TODO Gather statistics about the length of the waiter queue.
	 */

	/* Per port queue for send and receive buffers that could not be
	 * enqueued in one of the other queues or lists because they were
	 * busy.
	 */
	struct queue		*waiter;
};

/*
 * Constructor for struct comm.
 */
int comm_ctor(struct comm *self, struct alloc *alloc,
              struct network *net, struct buffer_pool *bufpool,
              ll sendqsz, ll recvqsz);

/*
 * Destructor for struct comm.
 */
int comm_dtor(struct comm *self);

/*
 * Start the communication thread. When comm_start_processing()
 * returns the communication thread is up and running and processes
 * sends and receives as well as connection setup.
 */
int comm_start_processing(struct comm *self);

/*
 * Temporarily stop the communication thread. This is useful since
 * the communication thread unfortunately needs to hold the net->lock
 * almost all of the time. If another thread likes to modify net
 * it may wait a long time to acquire the lock since it usually is
 * not a FIFO-type lock. If the communication thread is temporarily
 * stopped from processing new requests it will not try to acquire the
 * lock itself.
 */
int comm_stop_processing(struct comm *self);

/*
 * Resume the processing.
 */
int comm_resume_processing(struct comm *self);

/*
 * Halt (terminate) the communication thread.
 */
int comm_halt_processing(struct comm *self);

/*
 * Enqueue a buffer in the send queue. If the queue is full this call
 * will block.
 */
int comm_enqueue(struct comm *self, struct buffer *buffer);

/*
 * Dequeue a buffer from the receive queue. If the queue is empty
 * this call will block.
 */
int comm_dequeue(struct comm *self, struct buffer **buffer);

#endif

