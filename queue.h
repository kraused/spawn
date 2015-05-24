
#ifndef SPAWN_QUEUE_H_INCLUDED
#define SPAWN_QUEUE_H_INCLUDED 1

#include "ints.h"
#include "thread.h"


/*
 * A generic queue storing pointers to something. The size of
 * the queue is fixed but the queue can be resized.
 *
 * This structure is not thread-safe.
 */
struct queue
{
	struct alloc	*alloc;

	ll		capacity;
	void		**buf;

	ll		head;
	ll		tail;
	ll		size;	/* Size stored for faster access */
};

/*
 * Construct a new queue.
 */
int queue_ctor(struct queue *self, struct alloc *alloc, ll capacity);

/*
 * Destructor for a queue instance.
 */
int queue_dtor(struct queue *self);

/*
 * Increase or decrease the capacity of the queue. It is an error
 * to decrease the capacity below the current size of the queue.
 */
int queue_change_capacity(struct queue *self, ll capacity);

/*
 * Get the capacity of the queue.
 */
static inline void queue_capacity(struct queue *self, ll *capacity)
{
	*capacity = self->capacity;
}

/*
 * Get the size of the queue.
 */
static inline void queue_size(struct queue *self, ll *size)
{
	*size = self->size;
}

/*
 * Enqueue p.
 */
int queue_enqueue(struct queue *self, void *p);

/*
 * Dequeue an element from the queue.
 */
int queue_dequeue(struct queue *self, void **p);

/*
 * Peek at the head of the queue. The function returns the same as
 * queue_dequeue() but does not modify the queue.
 */
int queue_peek(struct queue *self, void **p);


/*
 * A thread-safe variant of struct queue that ensures consistency by means
 * of mutual exclusion.
 */
struct queue_with_lock
{
	struct queue	queue;
	struct lock	lock;
};

int queue_with_lock_ctor(struct queue_with_lock *self, struct alloc *alloc, ll capacity);
int queue_with_lock_dtor(struct queue_with_lock *self);

int queue_with_lock_size(struct queue_with_lock *self, ll *size);

int queue_with_lock_enqueue(struct queue_with_lock *self, void *p);
int queue_with_lock_dequeue(struct queue_with_lock *self, void **p);

int queue_with_lock_peek(struct queue_with_lock *self, void **p);

#endif

