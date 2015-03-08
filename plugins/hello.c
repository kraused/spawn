
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"


static int _local(struct task_plugin *self,
                  int argc, char **argv);
static int _other(struct task_plugin *self,
                  int argc, char **argv);
static void _log_argv(int argc, char **argv);

static struct task_plugin_ops _hello_ops = {
	.local = _local,
	.other = _other
};

static struct task_plugin _hello = {
	.base = {
		.name = "hello",
		.version = 1,
		.type = PLUGIN_TASK
	},
	.ops = &_hello_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_hello;
}

static int _local(struct task_plugin *self,
                  int argc, char **argv)
{
	log("Hello world!");
	_log_argv(argc, argv);

	return 0;
}

static int _other(struct task_plugin *self,
                  int argc, char **argv)
{
	log("Hello world!");
	_log_argv(argc, argv);

	return 0;
}

static void _log_argv(int argc, char **argv)
{
	int i;

	log("argc = %d", argc);
	for (i = 0; argv[i]; ++i)
		log("argv[%d] = %s", i, argv[i]);
}

