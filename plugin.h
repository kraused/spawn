
#ifndef SPAWN_PLUGIN_H_INCLUDED
#define SPAWN_PLUGIN_H_INCLUDED 1

struct plugin;
struct plugin_ops;
struct exec_plugin;
struct exec_plugin_opts;
struct task_plugin;
struct task_plugin_opts;


enum
{
	/* A plugin without any specific purpose.
	 */
	PLUGIN_NOTYPE = 0,
	/* Plugin providing remote spawning capabilities.
	 * See exec_plugin for details
	 */
	PLUGIN_EXEC,
	/* Plugin which defines the actual task to be executed
	 * on the hosts after successfull setup.
	 */
	PLUGIN_TASK
};


/*
 * Base class for all plugins.
 */
struct plugin
{
	const char		*name;
	int			version;
	int			type;

	void			*handle;

	struct plugin_ops	*ops;
};

/*
 * Fundamental operations for each plugin. Function pointers may
 * be NULL.
 */
struct plugin_ops
{
	/*
	 * FIXME Pass command line arguments/options as the
	 *       second argument.
	 */
	int	(*init)(struct plugin *self, void *opts);
	int	(*fini)(struct plugin *self);
};

/*
 * Plugin providing (remote) execution functionality.
 */
struct exec_plugin
{
	struct plugin		base;

	struct exec_plugin_ops	*ops;
};

/*
 * Operations provided by an "exec" plugin. Function pointers may be
 * NULL if the plugin does not provide the functionality.
 */
struct exec_plugin_ops
{
	/*
	 * FIXME Provide option/functionality for spawning
	 *	 on a remote host where the spawn util itself
	 * 	 is not available. This is possible for example
	 *       by first transferring all applications to the
	 *       remote host.
	 */
	int	(*exec)(struct exec_plugin *self,
		        const char *host,
	                char *const *argv);
};

/*
 * Task plugin. The task plugin provides the actual work to be executed once
 * we succesfully setup the network.
 */
struct task_plugin
{
	struct plugin		base;

	/* FIXME We could potentially support multiple tasks (running in separate
	 *       threads). Therefore we could organize the task plugins in a linked
	 *	 list.
	 *	 If we have multiple tasks we need to have some kind of multiplexing
	 *	 functionality. 
	 */

	struct task_plugin_ops	*ops;
};

/*
 * Operations provided by a "task" pluign
 */
struct task_plugin_ops
{
	int	(*task)(int argc, char **argv);
};


/*
 * Load a plugin from disk.
 */
struct plugin *load_plugin(const char *path);

/*
 * Check that the plugin is of type PLUGIN_EXEC and cast to exec_plugin
 * if the type is correct.
 */
struct exec_plugin *cast_to_exec_plugin(struct plugin *plu);

/*
 * Check that the plugin is of type PLUGIN_EXEC and cast to task_plugin
 * if the type is correct.
 */
struct task_plugin *cast_to_task_plugin(struct plugin *plu);

#endif

