
#ifndef SPAWN_HELPER_H_INCLUDED
#define SPAWN_HELPER_H_INCLUDED 1

#include "ints.h"

struct sockaddr;

#define MAX(X,Y)	(((X) >= (Y)) ? (X) : (Y))
#define MIN(X,Y)	(((X) >= (Y)) ? (Y) : (X))

#define ARRAYLEN(A)	((sizeof((A)))/(sizeof((A)[0])))

/*
 * Wrapper around close() that handles EINTR.
 */
int do_close(int fd);

/*
 * Wrapper around accept() that handles EINTR.
 */
int do_accept(int sockfd, struct sockaddr *addr, ll *addrlen);

/*
 * Our version of xstrup which uses the allocator alloc.
 */
int xstrdup(struct alloc *alloc, const char *istr, char **ostr);

/*
 * Duplicate an array of strings.
 */
int array_of_str_dup(struct alloc *alloc, int n, const char **istr,
                     char ***ostr);

/*
 * Free an array of strings.
 */
int array_of_str_free(struct alloc *alloc, int n, char ***str);

#endif

