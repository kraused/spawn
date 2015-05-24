
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "helper.h"
#include "ints.h"
#include "msgbuf.h"


static __thread char _msg1[4096];
static __thread char _msg2[4096];
static struct msgbuf *_bout = NULL;
static struct msgbuf *_berr = NULL;

static void _report(FILE *f, struct msgbuf *b, const char* prefix, const char* file,
                    const char* func, long line, const char* fmt,
                    va_list vl);


int register_io_buffers(struct msgbuf *bout, struct msgbuf *berr)
{
	_bout = bout;
	_berr = berr;

	return 0;
}

void spawn_error(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report(stderr, _berr, "error: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}

void spawn_warn(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report(stderr, _berr, "warning: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}

void spawn_log(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report(stdout, _bout, "", file, func, line, fmt, vl);
	va_end(vl);
}

void spawn_debug(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report(stderr, _berr, "debug: ", file, func, line, fmt, vl);
	va_end(vl);

	fflush(NULL);
}

void die()
{
	abort();
}


static void _report(FILE *f, struct msgbuf *b, const char* prefix, const char* file,
                    const char* func, long line, const char* fmt,
                    va_list vl)
{
	char time[64];
	struct timeval tv;
	struct tm tm;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);

	strftime(time, sizeof(time), "%FT%H%M%S", &tm);

	/* FIXME Add the hostname to the information.
	 */

	vsnprintf(_msg1, sizeof(_msg1), fmt, vl);
	snprintf(_msg2, sizeof(_msg2),
	         " %s.%06ld [%04d, %04d] (%s(), %s:%ld): %s%s\n",
	         time, (long )tv.tv_usec, (int )getpid(), (int )gettid(), func,
	         file, line, prefix, _msg1);

	if (b) {
		msgbuf_print(b, _msg2);
	} else {
		fprintf(f, "%s", _msg2);
		fflush(f);
	}
}

