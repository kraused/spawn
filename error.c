
#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include "compiler.h"
#include "error.h"


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

void spawn_info(const char* file, const char* func, long line, const char* fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	_report_to_stderr("info: ", file, func, line, fmt, vl);
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
	static char msg[4096];
    
	vsnprintf(msg, sizeof(msg), fmt, vl);
	fprintf(stderr, " [%s in %s:%ld] %s%s\n", file, func, 
		line, prefix, msg );
}

