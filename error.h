
#ifndef SPAWN_ERROR_H_INCLUDED
#define SPAWN_ERROR_H_INCLUDED 1

void spawn_error(const char* file, const char* func, long line, const char* fmt, ...);
void spawn_warn(const char* file, const char* func, long line, const char* fmt, ...);
void spawn_log(const char* file, const char* func, long line, const char* fmt, ...);

/*
 * Error handling marcos. We do not provide a fatal() macro to force hackers to perform
 * a orderly retreat when hitting an error.
 */
#define error(FMT, ...) spawn_error(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define warn(FMT, ...)	spawn_warn(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)
#define info(FMT, ...)	spawn_info(__FILE__, __func__, __LINE__, FMT, ## __VA_ARGS__)

#endif

