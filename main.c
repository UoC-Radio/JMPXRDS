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
#include "fmmod.h"
#include <stdlib.h>	/* For NULL */
#include <unistd.h>	/* For sleep() */
#include <stdio.h>	/* For printf */
#include <sched.h>	/* For sched_setscheduler etc */
#include <signal.h>	/* For signal handling */
#include <string.h>	/* For memset() */

volatile sig_atomic_t active;

static void
signal_handler(int sig)
{
	if(sig == SIGPIPE)
		return;
	active = 0;
}

int
main(int argc,char *argv[])
{
	int ret = 0;
	struct sched_param sched;
	struct fmmod_instance fmmod_instance;
	struct sigaction sa;

	memset(&sched, 0, sizeof(struct sched_param));
	memset(&sa, 0, sizeof(struct sigaction));

	sched_getparam(0, &sched);
	sched.sched_priority = 99;
	if (sched_setscheduler(0, SCHED_FIFO, &sched) != 0)
		perror("Unable to set real time scheduling:");
	
	ret = fmmod_initialize(&fmmod_instance, FMMOD_REGION_EU);
	if(ret < 0)
		exit(ret);

	active = 1;

	/* Install a signal handler for graceful exit 
	 * and for handling SIGPIPE */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = signal_handler;
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	printf("JMPXRDS Started\n");

	/* Keep running until the transport stops
	 * or in case we are interrupted */
	while (active && (fmmod_instance.active == 1))
		sleep(1);

	if(fmmod_instance.active)
		fmmod_destroy(&fmmod_instance);
	exit ( 0 );
}
