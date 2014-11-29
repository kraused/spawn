
#include <stdlib.h>
#include <dlfcn.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "plugin.h"


struct plugin *load_plugin(const char *path)
{
	void *handle;
	struct plugin *plu;
	struct plugin *(*construct)();

	dlerror();

	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (unlikely(!handle)) {
		error("dlopen() failed. dlerror() says '%s'.", dlerror());
		return NULL;
	}

	construct = dlsym(handle, "plugin_construct");
	if (unlikely(!construct)) {
		error("Module does not provide the 'plugin_construct' function.");
		dlclose(handle);
		return NULL;
	}

	plu = construct();
	if (unlikely(!plu)) {
		error("Plugin constructor failed.");
		dlclose(handle);
		return NULL;
	}

	plu->handle = handle;

	return plu;
}

struct exec_plugin *cast_to_exec_plugin(struct plugin *plu)
{
	if (likely(plu && (PLUGIN_EXEC == plu->type)))
		return (struct exec_plugin *)plu;

	return NULL;
}

struct task_plugin *cast_to_task_plugin(struct plugin *plu)
{
	if (likely(plu && (PLUGIN_TASK == plu->type)))
		return (struct task_plugin *)plu;

	return NULL;
}

