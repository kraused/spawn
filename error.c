
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "compiler.h"
#include "error.h"


static __thread char _msg[4096];

static void _report_to_stderr(const char* prefix, const char* file,
                              const char* func, long line, const char* fmt,
                              va_list vl);


void spawn_error(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report_to_stderr("error: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}

void spawn_warn(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report_to_stderr("warning: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}

void spawn_log(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report_to_stderr("", file, func, line, fmt, vl);
	va_end(vl);
}

void spawn_debug(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report_to_stderr("debug: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}


static void _report_to_stderr(const char* prefix, const char* file,
                              const char* func, long line, const char* fmt,
                              va_list vl)
{
	char time[64];
	struct timeval tv;

	gettimeofday(&tv, NULL);
	strftime(time, sizeof(time), "%FT%H%M%S", localtime(&tv.tv_sec));

	vsnprintf(_msg, sizeof(_msg), fmt, vl);
	fprintf(stderr, " %s.%06ld [%04d] (%s(), %s:%ld): %s%s\n",
	        time, (long )tv.tv_usec, (int )getpid(), func, file, line,
	        prefix, _msg);
}

void die()
{
	abort();
}

