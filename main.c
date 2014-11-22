
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"

static int _main_on_local(int argc, char **argv);
static int _main_on_other(int argc, char **argv);

int main(int argc, char **argv)
{
	int n, m;
	int ret;

	/* This is a simple method to recognize whether to call
	 * _main_on_other or _main_on_local. On the remote side
	 * we always execute the libexec spawn executable using
	 * a full path (PATH is empty). If argv[0] ends with
	 * "libexec/spawn" we assume that _main_on_other is the
	 * next step. This should be accurate in 99% of the cases
	 * and in 100% of the cases where the application is
	 * used as intended.
	 */

	n = strlen(argv[0]);
	m = sizeof("libexec/spawn") - 1;

	if ((n >= m) && !strcmp(argv[0] + n - m, "libexec/spawn"))
		ret = _main_on_other(argc, argv);
	else
		ret = _main_on_local(argc, argv);

	return 0;
}

static int _main_on_local(int argc, char **argv)
{
	return 0;
}

static int _main_on_other(int argc, char **argv)
{
	return 0;
}

