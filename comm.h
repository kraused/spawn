
#ifndef SPAWN_COMM_H_INCLUDED
#define SPAWN_COMM_H_INCLUDED 1

#include "ints.h"
#include "thread.h"
#include "queue.h"

struct alloc;
struct buffer;
struct message_header;

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
	struct queue_with_lock	sendq;
	struct queue_with_lock	recvq;

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

	/* For ppoll() syscall. */
	int			nrwfds;		/* Number of fds for incoming
						 * and outgoing traffic. */
	int			nlistenfds;	/* Number of fds used to listen
						 * for connections. */
	int			npollfds;	/* == nlistenfds + nrwfds */
	struct pollfd		*pollfds;	/* The read/write fds come first.
						 */

	/* Buffers for currently ongoing send and receive operations
	 * ordered according to the port. Size is net->nports.
	 */
	struct buffer		**recvb;
	struct buffer		**sendb;

	/* Storage for outgoing broadcast messages.
	 */
	struct buffer		*bcastb;
	int			bcastp;

	/* Next free channel returned by comm_rescv_channel().
	 */
	ui16			channel;
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

/*
 * The function comm_dequeue_would_succeed() returnes true (1)
 * if comm_dequeue() would have succeeded and not returned -ENOENT.
 * Note that a subsequent comm_dequeue() call could still return -ENOENT
 * if a different thread dequeues a buffer between the two calls.
 */
int comm_dequeue_would_succeed(struct comm *self, int *result);

/*
 * Flush the communication queues. Block until the send queue is empty
 * and all packages are routed.
 */
int comm_flush(struct comm *self);

/*
 * Reserve a virtual channel.
 */
int comm_resv_channel(struct comm *self, ui16 *channel);

/*
 * Internal function exported for use by _recv_join_response() in main.c.
 */
int secretly_copy_header(struct buffer *buffer,
                         struct message_header *header);

#endif

