
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
static const char devel_hosts[] = "a[01-20],a35";

/*
 * The expanded host list will be created from devel_hosts
 * by a function.
 */
static const char *devel_hostlist[] = {
	"a01", "a02", "a03", "a04", "a05", "a06", "a07", "a08",
	"a09", "a10", "a11", "a12", "a13", "a14", "a15", "a16",
	"a17", "a18", "a19", "a20", "a35", "localhost", "localhost"
};
static const int devel_nhosts = sizeof(devel_hostlist)/sizeof(devel_hostlist[0]);

#endif

