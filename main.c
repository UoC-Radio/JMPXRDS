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
#include "utils.h"
#include <stdlib.h>		/* For NULL */
#include <unistd.h>		/* For sleep() */
#include <stdio.h>		/* For printf */
#include <sched.h>		/* For sched_setscheduler etc */
#include <signal.h>		/* For signal handling / sig_atomic_t */
#include <string.h>		/* For memset() */

#ifdef DEBUG
#include <execinfo.h>		/* For backtrace() etc */
#include <ucontext.h>
#endif

static volatile sig_atomic_t active;

static void
signal_handler(int sig, siginfo_t * info,
	       __attribute__((unused)) void *context)
{
#ifdef DEBUG
	void *bt[16] = {0};
	int bt_size = 0;
	char **messages = NULL;
	int i = 0;
#endif
	switch (sig) {
	case SIGPIPE:
		return;
	case SIGUSR1:
		rtp_server_add_receiver(info->si_value.sival_int);
		break;
	case SIGUSR2:
		rtp_server_remove_receiver(info->si_value.sival_int);
		break;
	case SIGABRT:
		utils_err("[MAIN] Got abort at %p \n",
			  info->si_addr);
#ifdef DEBUG
		if(info->si_addr) {
			bt_size = backtrace(bt, 16);
			messages = backtrace_symbols(bt, bt_size);
			utils_trace("Backtrace:\n");
			for(i= 1; i < bt_size; ++i)
				utils_trace("\t%s\n", messages[i]);
		}
#endif
		utils_shm_unlink_all();
		raise(SIGKILL);
		break;
	case SIGSEGV:
		utils_err("[MAIN] Got segfault at %p \n",
			  info->si_addr);
#ifdef DEBUG
		if(info->si_addr) {
			bt_size = backtrace(bt, 16);
			messages = backtrace_symbols(bt, bt_size);
			utils_trace("Backtrace:\n");
			for(i= 1; i < bt_size; ++i)
				utils_trace("\t%s\n", messages[i]);
		}
#endif
		utils_shm_unlink_all();
		raise(SIGKILL);
		break;
	case SIGQUIT:
#ifdef DEBUG
	__gcov_flush();
#endif
	default:
		active = 0;
		break;
	}

	return;
}

int
main()
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
		utils_perr("[MAIN] Unable to set real time scheduling:");

	ret = fmmod_initialize(&fmmod_instance);
	if (ret < 0)
		exit(ret);

	active = 1;

	/* Install a signal handler for graceful exit 
	 * and for handling SIGPIPE */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = signal_handler;
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);

	utils_ann("JMPXRDS Started\n");

	/* Keep running until the transport stops
	 * or in case we are interrupted */
	while (active && fmmod_instance.active)
		sleep(1);

	if (fmmod_instance.active)
		fmmod_destroy(&fmmod_instance, 0);

	exit(0);
}
