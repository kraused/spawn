
#ifndef SPAWN_LIST_H_INCLUDED
#define SPAWN_LIST_H_INCLUDED 1

#include <stdlib.h>

#include "ints.h"


/*
 * (Double-)linked list data structure. The idea for the implementation
 * follows the version in the Linux kernel.
 */

/*
 * Linked list member for embedding into the user
 * data structure
 */
struct list
{
	struct list *prev, *next;
};

/*
 * Constructor for a list.
 */
static inline void list_ctor(struct list *self)
{
	self->prev = self;
	self->next = self;
}

/*
 * Destructor for a list. Moderately useful.
 */
static inline void list_dtor(struct list *self)
{
	self->prev = NULL;
	self->next = NULL;
}

static inline void list_insert_inbetween(struct list *entry, struct list *prev, struct list *next)
{
	next->prev = entry;
	prev->next = entry;
	entry->prev = prev;
	entry->next = next;
}

/*
 * Insert entry after self.
 */
static inline void list_insert_after(struct list *self, struct list *entry)
{
	list_insert_inbetween(entry, self, self->next);
}

/*
 * Insert entry before self.
 */
static inline void list_insert_before(struct list *self, struct list *entry)
{
	list_insert_inbetween(entry, self->prev, self);
}

static inline void list_remove(struct list *self)
{
	self->prev->next = self->next;
	self->next->prev = self->prev;

	list_dtor(self);
}

#define LIST_ENTRY(p, T, member)	\
	((T *)(((char *)p) - (ull)(&((const T *)0)->member)))

#define LIST_FOREACH(p, head)		\
	for ((p) = (head)->next; (p) != (head); (p) = (p)->next)

/*
 * Safe variant of LIST_FOREACH which uses a temporary variable such that it is possible
 * to iterate over the list while removing entries from it.
 */
#define LIST_FOREACH_S(p, tmp, head)	\
	for ((p) = (head)->next, (tmp) = (p)->next; (p) != (head); (p) = (tmp), (tmp) = (p)->next)

#endif

