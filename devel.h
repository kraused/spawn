
#ifndef SPAWN_DEVEL_H_INCLUDED
#define SPAWN_DEVEL_H_INCLUDED 1

/*
 * This is a development header that should help during the
 * development of the program by hardcoding certain data and
 * logic that should be variable in the final release.
 *
 * MAKE SURE THIS HEADER IS GONE BEFORE RELEASING THE SOFTWARE!
 */

/*
 * A compressed host list will be the user input. This will be
 * stored somewhere in the option list. 
 *
 * -o Hosts="a[01-10],a15"
 */
static const char spawn_devel_hosts[] = "a[01-20],a35";

/*
 * The expanded host list will be created from spawn_devel_hosts
 * by a function.
 */
static const char *spawn_devel_hostlist[] = {
	"a01", "a02", "a03", "a04", "a05", "a06", "a07", "a08",
	"a09", "a10", "a11", "a12", "a13", "a14", "a15", "a16",
	"a17", "a18", "a19", "a20", "a35"
};
static const int spawn_devel_nhosts = sizeof(spawn_devel_hostlist)/sizeof(spawn_devel_hostlist[0]);

/*
 * The tree width is read from the command line.
 *
 * -o TreeWidth=3 
 */
static const int spawn_devel_tree_width = 4;

/*
 * Fanout width. This parameter specifies how many processes are spawned in parallel. Note
 * that even for verly large fanout settings processes are still spawned in groups during the
 * construction of the tree from the group up.
 *
 * -o Fanout=128
 */
static const int spawn_devel_fanout = 2;

/*
 * For testing: Use an incomplete exec plugin that just spawns the process locally.
 */
#define EXEC_PLUGIN SPAWN_INSTALL_PREFIX "/lib/local.so"

#endif

