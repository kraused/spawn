
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

	/* Queues for send requests and incoming messages. */
	struct comm_queue	sendq;
	struct comm_queue	recvq;

	/* Set to one to force the shutdown of the communication
	 * thread.
	 */
	volatile int		stop;

	/* Thread handle for the communication thread
	 */
	struct thread		thread;
};

/*
 * Constructor for struct comm.
 */
int comm_ctor(struct comm *self, struct alloc *alloc, struct network *net,
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
 * Stop the communication thread.
 */
int comm_stop_processing(struct comm *self);

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

