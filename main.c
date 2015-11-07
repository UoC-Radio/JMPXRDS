/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Main loop
 *
 * Copyright (C) 2015 Nick Kossifidis <mickflemm@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h> /* For NULL */
#include <unistd.h> /* For sleep() */
#include <stdio.h> /* For printf */
#include <sched.h> /* For sched_setscheduler etc */
#include <signal.h> /* For signal handling */
#include "fmmod.h"

volatile sig_atomic_t active;

static void
signal_handler(int sig)
{
	active = 0;
	fprintf (stderr, "signal received, exiting ...\n");
	exit (0);
}

int
main(int argc,char *argv[])
{
	int ret = 0;
	struct sched_param sched;
	struct fmmod_instance fmmod_instance;
	struct sigaction sa;

	sched_getparam(0, &sched);
	sched.sched_priority = 99;
	if (sched_setscheduler(0, SCHED_FIFO, &sched) != 0)
		perror("Unable to set real time scheduling:");
	
	ret = fmmod_initialize(&fmmod_instance, REGION_EU);
	if(ret < 0)
		exit(ret);

	active = 1;

	/* Install a signal handler for graceful exit */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = signal_handler;
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	printf("JMPXRDS Started\n");

	/* Keep running until the transport stops
	 * or in case we are interrupted */
	while (active)
		sleep(1);

	fmmod_destroy(&fmmod_instance);
	exit ( 0 );
}
