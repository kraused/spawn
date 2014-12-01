
#ifndef SPAWN_ATOMIC_H_INCLUDED
#define SPAWN_ATOMIC_H_INCLUDED 1

/*
 * Atomic operations on integer types. Not really portable
 * as of this writing.
 */

#define atomic_read(x)			(*(volatile typeof(x)*)&(x))
#define atomic_write(x, val)		do { (x) = (val); } while(0)

/*
 * Compare-and-swap operation.
 */
#define atomic_cmpxchg(x, oval, nval)	__sync_val_compare_and_swap((volatile typeof(x)*)&(x), (oval), (nval))

/*
 * Fetch-and-add operation.
 */
#define atomic_xadd(x, val)		__sync_fetch_and_add((volatile typeof(x)*)&(x), (val))

#endif

