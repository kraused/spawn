
#ifndef SPAWN_WATCHDOG_H_INCLUDED
#define SPAWN_WATCHDOG_H_INCLUDED

/*
 * The watchdog thread is a special security measure to make sure that
 * no residual processes are left on remote nodes when the connection
 * dies for some reason. If the function calm_the_watchdog() is not called
 * for timeout seconds the watchdog will forcibly terminate this process
 * and all subprocesses.
 */
int let_the_watchog_loose(int timeout);

/*
 * Declare that everything is alread. This function resets the watchdog
 * timer.
 */
int calm_the_watchdog();

#endif

