
#ifndef SPAWN_COMPILER_H_INCLUDED
#define SPAWN_COMPILER_H_INCLUDED 1

/*
 * Internal header.
 */

#define likely(X)	__builtin_expect(!!(X), 1)
#define unlikely(X)	__builtin_expect(!!(X), 0)

#endif

