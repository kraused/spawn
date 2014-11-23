
#include <stdio.h>

#include "compiler.h"
#include "error.h"
#include "plugin.h"


static int _exec(struct exec_plugin *self,
                 const char *host,
                 char *const *argv);

static struct exec_plugin_ops _ssh_ops = {
	.exec = _exec
};

static struct exec_plugin _ssh = {
	.base = {
		.name = "ssh",
		.version = 1,
		.type = PLUGIN_EXEC
	},
	.ops = &_ssh_ops
};

struct plugin *plugin_construct()
{
	static int init = 0;

	if (0 != init) {
		error("plugin_construct() should only be called once.");
		return NULL;
	}
	init = 1;

	return (struct plugin *)&_ssh;
}


static int _exec(struct exec_plugin *self,
                 const char *host,
                 char *const *argv)
{
	return -1;
}

