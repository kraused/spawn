
#ifndef SPAWN_ERROR_H_INCLUDED
#define SPAWN_ERROR_H_INCLUDED 1

#include <errno.h>

/* Some custom errnos that seem useful */
#define ESOMEFAULT	201	/* Something bad happend */
#define ENOTIMPL	202	/* Not implemented yet */

#include <assert.h>

/*
 * Error handling marcos. We do not provide a fatal() macro to force hackers to perform
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
 * Terminate the application.
 */
void die() __attribute__((noreturn));

#endif

