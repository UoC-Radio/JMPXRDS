/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RTP server runtime configuration tool
 *
 * Copyright (C) 2016 Nick Kossifidis <mickflemm@gmail.com>
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

#include "utils.h"
#include "rtp_server.h"
#include <signal.h>		/* For sigqueue etc */
#include <arpa/inet.h>		/* For inet_addr etc */
#include <unistd.h>		/* For getopt() */

static void
usage(char *name)
{
	utils_ann("RTP Server configuration tool for JMPXRDS\n");
	utils_info("Usage: %s -g or [<parameter> <value>] pairs\n", name);
	utils_info("\nParameters:\n"
		"\t-g\t\tGet current status\n"
		"\t-a   <string>\tAdd an IP address to the list of receivers\n"
		"\t-r   <string>\tRemove an IP address from the list of receivers\n");
}

int
main(int argc, char *argv[])
{
	union sigval value;
	struct shm_mapping *shmem = NULL;
	struct rtp_server_control *ctl = NULL;
	struct in_addr ipv4addr = { 0 };
	int opt = 0;
	int ret = 0;
	int pid = 0;
	int i = 0;

	shmem = utils_shm_attach(RTP_SRV_SHM_NAME,
				 sizeof(struct rtp_server_control));
	if (!shmem) {
		utils_perr("Unable to communicate with the RTP server");
		return -1;
	}
	ctl = (struct rtp_server_control*) shmem->mem;
	pid = ctl->pid;

	while ((opt = getopt(argc, argv, "ga:r:")) != -1)
		switch (opt) {
		case 'g':
			utils_info("RTP bytes sent: %" PRIu64 "\n",
			       ctl->rtp_bytes_sent);
			utils_info("RTCP bytes sent: %" PRIu64 "\n",
			       ctl->rtcp_bytes_sent);
			utils_info("List of receivers:\n");
			for (i = 0; i < ctl->num_receivers; i++) {
				ipv4addr.s_addr = ctl->receivers[i];
				utils_info("\t%s\n", inet_ntoa(ipv4addr));
			}
			break;
		case 'a':
			ret = inet_aton(optarg, &ipv4addr);
			if (!ret) {
				utils_err("Invalid IP address !\n");
				break;
			}
			value.sival_int = ipv4addr.s_addr;
			if (sigqueue(pid, SIGUSR1, value) != 0)
				utils_perr("Couldn't send signal, sigqueue()");
			break;
		case 'r':
			ret = inet_aton(optarg, &ipv4addr);
			if (!ret) {
				utils_err("Invalid IP address !\n");
				break;
			}
			value.sival_int = ipv4addr.s_addr;
			if (sigqueue(pid, SIGUSR2, value) != 0)
				utils_perr("Couldn't send signal, sigqueue()");
			break;
		default:
			usage(argv[0]);
			utils_shm_destroy(shmem, 0);
			return -1;
		}

	if (argc < 2 || (argc > 1 && optind == 1)) {
		usage(argv[0]);
		ret = -1;
	}

	utils_shm_destroy(shmem, 0);
	return ret;
}
