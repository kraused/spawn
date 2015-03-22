
#ifndef SPAWN_PLUGIN_H_INCLUDED
#define SPAWN_PLUGIN_H_INCLUDED 1

/* FIXME
 */
struct spawn;

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
	 * on the hosts after successful setup.
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
	/* FIXME init() and fini() are never called.
	 */
	/* FIXME Pass command line arguments/options as the
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
	/* TODO Provide option/functionality for spawning
	 *	on a remote host where the spawn util itself
	 * 	is not available. This is possible for example
	 *      by first transferring all applications to the
	 *      remote host.
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
	struct plugin			base;

	struct task_plugin_avail_ops	*api;
	void				*apix;	/* Auxiliary variable used to store
						 * everything the api functions require
						 * to function. */

	/* FIXME
	 */
	struct spawn			*spawn;

	struct task_plugin_ops		*ops;
};

/*
 * Operations provided by a "task" plugin
 */
struct task_plugin_ops
{
	/* main() function executed in the context of the master process.
	 * The function must canceable - If necessary by calling pthread_testcancel()
	 * at times.
	 */
	int	(*local)(struct task_plugin *self,
                         int argc, char **argv);
        /* main() function executed in the context of a remote process.
	 * The function must canceable - If necessary by calling pthread_testcancel()
	 * at times.
	 */
	int	(*other)(struct task_plugin *self,
                         int argc, char **argv);
};

/*
 * Operations available to a "task" plugin
 */
struct task_plugin_avail_ops
{
	/* Write a string to the standard output at the root of the tree.
	 */
	int	(*send_write_stdout)(struct task_plugin *plu, const char *str);
	/* Write a string to the standard error at the root of the tree.
	 */
	int	(*send_write_stderr)(struct task_plugin *plu, const char *str);
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

