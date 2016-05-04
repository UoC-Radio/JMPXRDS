#include "rtp_server.h"
#include <fcntl.h>	/* For O_* and F_* constants */
#include <unistd.h>	/* For ftruncate(), close() */
#include <string.h>	/* For memset() */
#include <sys/mman.h>	/* For shm_open */
#include <signal.h>	/* For sigqueue etc */
#include <arpa/inet.h>	/* For inet_addr etc */
#include <stdio.h>	/* For printf() */
#include <stdlib.h>	/* For atoi() */

static void
usage(char *name)
{
	printf("RTP Configuration tool for JMPXRDS\n");
	printf("\nUsage: %s -g or [<parameter> <value>] pairs\n", name);
	printf("\nParameters:\n"
		"\t-g\t\tGet current status\n"
		"\t-a   <string>\tAdd an IP address to the list of receivers\n"
		"\t-r   <string>\tRemove an IP address from the list of receivers\n");
}

static struct rtp_server_control *
rtp_server_tool_get_ctl()
{
	int ctl_fd = 0;
	int ret = 0;
	struct rtp_server_control *ctl = NULL;

	ctl_fd = shm_open(RTP_SRV_SHM_NAME, O_RDONLY, 0600);
	if(ctl_fd < 0)
		goto cleanup;

	ctl = (struct rtp_server_control*)
				mmap(0, sizeof(struct rtp_server_control),
				     PROT_READ, MAP_SHARED,
				     ctl_fd, 0);
 cleanup:
	close(ctl_fd);
	return ctl;
}

int main(int argc, char *argv[])
{
	union sigval value;
	struct rtp_server_control *ctl = NULL;
	struct in_addr ipv4addr = {0};
	int opt = 0;
	int ret = 0;
	int pid = 0;
	int i = 0;
	
	ctl = rtp_server_tool_get_ctl();
	if(!ctl) {
		perror("Unable to communicate with the RTP server");
		return -1;
	}
	pid = ctl->pid;

	while((opt = getopt(argc, argv, "ga:r:")) != -1)
		switch(opt) {
		case 'g':
			printf("RTP bytes sent: %" PRIu64 "\n", ctl->rtp_bytes_sent);
			printf("RTCP bytes sent: %" PRIu64 "\n", ctl->rtcp_bytes_sent);
			printf("List of receivers:\n");
			for(i = 0; i < ctl->num_receivers; i++) {
				ipv4addr.s_addr = ctl->receivers[i];
				printf("\t%s\n", inet_ntoa(ipv4addr));
			}
			break;
		case 'a':
			ret = inet_aton(optarg, &ipv4addr);
			if(!ret) {
				printf("Invalid IP address !\n");
				break;
			}
			value.sival_int = ipv4addr.s_addr;
			if(sigqueue(pid, SIGUSR1, value) != 0)
				perror("sigqueue():");
			break;
		case 'r':
			ret = inet_aton(optarg, &ipv4addr);
			if(!ret) {
				printf("Invalid IP address !\n");
				break;
			}
			value.sival_int = ipv4addr.s_addr;
			if(sigqueue(pid, SIGUSR2, value) != 0)
				perror("sigqueue():");
			break;
		default:
			usage(argv[0]);
			exit(-EINVAL);
	}

	if(!argc || (argc > 1 && optind == 1)) {
		usage(argv[0]);
		exit(-EINVAL);
	}

	return 0;
}
