
#ifndef SPAWN_ERROR_H_INCLUDED
#define SPAWN_ERROR_H_INCLUDED 1

#include <errno.h>

/* Some custom errnos that seem useful */
#define ESOMEFAULT	201	/* Something bad happend */
#define ENOTIMPL	202	/* Not implemented yet */

#include <assert.h>

struct msgbuf;

/*
 * By default error() and friends write to stdout or stderr. As an alternative, it is
 * possible to register a message buffer to which all output is routed. This is useful
 * for remotely spawned processes that have no stdout/stderr available.
 */
int register_io_buffers(struct msgbuf *bout, struct msgbuf *berr);

/*
 * Error handling macros. We do not provide a fatal() macro to force hackers to perform
 * a orderly retreat when hitting an error.
 */
#define error(FMT, ...)	spawn_error(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define warn(FMT, ...)	spawn_warn(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define log(FMT, ...)	spawn_log(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define debug(FMT, ...)	spawn_debug(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)

void spawn_error(const char* file, const char* func, long line, const char* fmt, ...);
void spawn_warn(const char* file, const char* func, long line, const char* fmt, ...);
void spawn_log(const char* file, const char* func, long line, const char* fmt, ...);
void spawn_debug(const char* file, const char* func, long line, const char* fmt, ...);

/*
 * Macro used to report error values returned by a function call in a convenient,
 * short and streamlined fashion.
 */
#define fcallerror(F, retval)	error(F "() failed with error %d.", retval)

/*
 * Terminate the application.
 */
void die() __attribute__((noreturn));

#endif

