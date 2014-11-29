
#ifndef SPAWN_QUEUE_H_INCLUDED
#define SPAWN_QUEUE_H_INCLUDED 1

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
 * Queue the size of the queue.
 */
static inline int queue_size(struct queue *self, ll *size)
{
	*size = self->size;

	return 0;
}

/*
 * Enqueue p.
 */
int queue_enqueue(struct queue *self, void *p);

/*
 * Dequeue an element from the queue.
 */
int queue_dequeue(struct queue *self, void **p);

#endif

