
#ifndef SPAWN_CONFIG_H_INCLUDED
#define SPAWN_CONFIG_H_INCLUDED 1

#define	SPAWN_EXE_LOCAL		SPAWN_INSTALL_PREFIX "/bin/spawn"
#define SPAWN_EXE_OTHER		SPAWN_INSTALL_PREFIX "/libexec/spawn"
#define SPAWN_CONFIG_DEFAULT	SPAWN_INSTALL_PREFIX "/etc/config.default"

/*
 * Maximal length of a filesystem path.
 */
#define SPAWN_MAX_PATH_LEN	512

/*
 * Default value. Can be overwritten using the command line options but
 * this will only take effect once the _join() function in _main_on_other()
 * returns.
 */
#define SPAWN_WATCHDOG_DEFAULT_TIMEOUT	180

#endif

