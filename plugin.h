
#ifndef SPAWN_PLUGIN_H_INCLUDED
#define SPAWN_PLUGIN_H_INCLUDED 1

struct plugin;
struct plugin_ops;
struct plugin_exec;
struct plugin_exec_opts;
struct plugin_task;
struct plugin_task_opts;


enum
{
	/* A plugin without any specific purpose.
	 */
	PLUGIN_NOTYPE = 0,
	/* Plugin providing remote spawning capabilities.
	 * See plugin_exec for details
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
struct plugin_exec
{
	struct plugin		base;

	struct plugin_exec_ops	*ops;
};

/*
 * Operations provided by an "exec" plugin. Function pointers may be
 * NULL if the plugin does not provide the functionality.
 */
struct plugin_exec_ops
{
	/*
	 * FIXME Provide option/functionality for spawning
	 *	 on a remote host where the spawn util itself
	 * 	 is not available. This is possible for example
	 *       by first transferring all applications to the
	 *       remote host.
	 */
	int	(*exec)(struct plugin_exec *self,
		        const char *host,
	                char *const *argv);
};

/*
 * Task plugin. The task plugin provides the actual work to be executed once
 * we succesfully setup the network.
 */
struct plugin_task
{
	struct plugin		base;

	/* FIXME We could potentially support multiple tasks (running in separate
	 *       threads). Therefore we could organize the task plugins in a linked
	 *	 list.
	 *	 If we have multiple tasks we need to have some kind of multiplexing
	 *	 functionality. 
	 */

	struct plugin_task_ops	*ops;
};

/*
 * Operations provided by a "task" pluign
 */
struct plugin_task_ops
{
	int	(*task)(int argc, char **argv);
};


/*
 * Load a plugin from disk.
 */
struct plugin *load_plugin(const char *path);

/*
 * Check that the plugin is of type PLUGIN_EXEC and cast to plugin_exec
 * if the type is correct.
 */
struct plugin_exec *cast_to_plugin_exec(struct plugin *plu);

/*
 * Check that the plugin is of type PLUGIN_EXEC and cast to plugin_task
 * if the type is correct.
 */
struct plugin_task *cast_to_plugin_task(struct plugin *plu);

#endif

