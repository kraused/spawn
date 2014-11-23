
#ifndef SPAWN_ALLOC_H_INCLUDED
#define SPAWN_ALLOC_H_INCLUDED 1

#include "ints.h"

struct alloc;
struct alloc_ops;

/*
 * Allocator structure. Implementations will usually wrap this structure and set a custom
 * alloc_ops pointer.
 */
struct alloc
{
	struct alloc_ops *ops;
};

/*
 * Allocator operations. All ("heavy") functions take information about the location of
 * the invocation to aid debugging.
 */
struct alloc_ops
{
	/* Allocate num elements of size sz each. The result will be a buffer of size (at least) num*sz.
	 */
	int	(*malloc)(struct alloc *self, void **p, ll num, ll sz, 
		          const char* file, const char* func, long line, const char* fmt, ...);
	/* Same as malloc except that the resulting buffer is guaranteed to be zeroed. Note that this means
	 * that the corresponding pages are touched by the allocating thread which may impact the memory
	 * placmement on NUMA machines (first touch policy).
	 */
	int	(*zalloc)(struct alloc *self, void **p, ll num, ll sz, 
		          const char* file, const char* func, long line, const char* fmt, ...);
	/* Reallocate a buffer that was allocated with malloc or zalloc passing onum and osz such that the
	 * new buffer has place for nnum elements of size nsz each.
	 */
	int	(*realloc)(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
		           const char* file, const char* func, long line, const char* fmt, ...);
	/* Variant of realloc. If realloc increased the storage space this function guarantees that the
	 * additional memory is filled with zeroes.
	 */
	int	(*zrealloc)(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
		            const char* file, const char* func, long line, const char* fmt, ...);
	/* 
	 */
	int	(*free)(struct alloc *self, void **p, ll num, ll size, 
	                const char* file, const char* func, long line, const char* fmt, ...);
	/* Same as free but clears memory content before freeing the allocation.
	 */
	int	(*zfree)(struct alloc *self, void **p, ll num, ll size, 
	                 const char* file, const char* func, long line, const char* fmt, ...);
};

#define	MALLOC(SELF, P, NUM, SZ, FMT, ...) \
	(SELF)->ops->malloc((SELF), (P), (NUM), (SZ), \
	                    __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define	ZALLOC(SELF, P, NUM, SZ, FMT, ...) \
	(SELF)->ops->zalloc((SELF), (P), (NUM), (SZ), \
	                    __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define REALLOC(SELF, P, ONUM, OSZ, NNUM, NSZ, FMT, ...) \
	(SELF)->ops->realloc((SELF), (P), (ONUM), (OSZ), (NNUM), (NSZ), \
	                     __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define ZREALLOC(SELF, P, ONUM, OSZ, NNUM, NSZ, FMT, ...) \
	(SELF)->ops->zrealloc((SELF), (P), (ONUM), (OSZ), (NNUM), (NSZ), \
	                      __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define FREE(SELF, P, NUM, SZ, FMT, ...) \
	(SELF)->ops->free((SELF), (P), (NUM), (SZ), \
	                  __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define ZFREE(SELF, P, NUM, SZ, FMT, ...) \
	(SELF)->ops->zfree((SELF), (P), (NUM), (SZ), \
	                   __FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)


/*
 * get a pointer to an allocator that works directly on top of the malloc/calloc/free functions provided by libc.
 * Even though this function may return different alloc instances when called multiple times all these allocators
 * use the same pool size.
 * The allocator will not make use of the debug information provided.
 */
struct alloc *libc_allocator();

/*
 * Get a pointer to an allocator that works directly on top of malloc/calloc/free - just like libc_allocator() - but
 * additionally provides debugging information. Each operation will result in an info() message send to stderr and thus
 * may slowdown the execution significantly.
 *
 * TODO Provide an option for a ring buffer that can be written to for debugging purpose.
 *
 */
struct alloc *libc_allocator_with_debugging();

#endif

