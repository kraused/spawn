
#ifndef SPAWN_HELPER_H_INCLUDED
#define SPAWN_HELPER_H_INCLUDED 1

#include "ints.h"

struct pollfd;
struct sockaddr;
struct alloc;

#define MAX(X,Y)	(((X) >= (Y)) ? (X) : (Y))
#define MIN(X,Y)	(((X) >= (Y)) ? (Y) : (X))

#define ARRAYLEN(A)	((sizeof((A)))/(sizeof((A)[0])))

/*
 * Convert four integers to a 32-bit IPv4 address in host
 * byte order.
 */
#undef  IP4ADDR
#define IP4ADDR(x,y,z,w) ((x ## u << 24) | (y ## u << 16) << (z ## u << 8) | (w ## u))

/*
 * Wrapper around close() that handles EINTR.
 */
int do_close(int fd);

/*
 * Wrapper around accept() that handles EINTR.
 */
int do_accept(int sockfd, struct sockaddr *addr, ll *addrlen);

/*
 * Wrapper around connect() that handles EINTR.
 */
int do_connect(int sockfd, struct sockaddr *addr, ll addrlen);

/*
 * Wrapper around poll(). In contrast to poll() the number of
 * structures which have nonzero revents field is returned as
 * a separate argument.
 */
int do_poll(struct pollfd *fds, int nfds, int timeout, int *num);

/*
 * Wrapper around write() that handles EINTR. The number of bytes
 * written is returned as last argument.
 */
int do_write(int fd, void *buf, ll size, ll *bytes);

/*
 * Wrapper around read() that handles EINTR. The number of bytes
 * read is returned as last argument.
 */
int do_read(int fd, void *buf, ll size, ll *bytes);

/*
 * Our version of strdup which uses the allocator alloc.
 */
int xstrdup(struct alloc *alloc, const char *istr, char **ostr);

/*
 * Simple ZFREE() for null-terminated strings. strfree can be
 * used together with xstrdup(). strfree() can handle NULL pointers
 * and will do nothing in this case.
 */
int strfree(struct alloc *alloc, char **str);

/*
 * Duplicate an array of strings.
 */
int array_of_str_dup(struct alloc *alloc, int n, const char **istr,
                     char ***ostr);

/*
 * Free an array of strings.
 */
int array_of_str_free(struct alloc *alloc, int n, char ***str);

/*
 * Daemonize the process.
 */
int daemonize();

/*
 * Wrapper around syscall(SYS_gettid)
 */
ll gettid();

/*
 * Get the seconds since the start of the epoch.
 */
ll llnow();

#endif

