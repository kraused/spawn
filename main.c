
#include "config.h"


static int _main_on_local(int argc, char **argv);
static int _main_on_other(int argc, char **argv);

static char argv0[SPAWN_MAX_PATH_LEN];

int main(int argc, char **argv)
{
	/* Expand argv[0] to full path */	

	/* Call _main_on_local or _main_on_other depending on
	   argv[0] */

	return _main_on_local(argc, argv);
}


static int _main_on_local(int argc, char **argv)
{
	return 0;
}

static int _main_on_other(int argc, char **argv)
{
	return 0;
}

