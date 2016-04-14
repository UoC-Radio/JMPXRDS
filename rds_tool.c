/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RDS runtime configuration tool
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
#include "rds_encoder.h"
#include <stdlib.h>	/* For atoi / strtol */
#include <stdio.h>	/* For printf / snprintf */
#include <unistd.h>	/* For ftruncate */
#include <string.h>	/* For memset / strnlen / strncmp */
#include <sys/mman.h>	/* For shm_open */
#include <fcntl.h>	/* For O_* constants */
#include <sys/stat.h>	/* For mode constants */

#define TEMP_BUF_LEN	RDS_RT_LENGTH + 1
void
usage(char *name)
{
	printf("RDS Configuration tool for JMPXRDS\n");
	printf("\nUsage: %s -g or [<parameter> <value>] pairs\n", name);
	printf("\nParameters:\n"
		"\t-g\t\tGet current config\n"
		"\t-e          \tEnable RDS encoder\n"
		"\t-d          \tDisable RDS encoder\n"
		"\t-rt   <text>\tSet radiotext\n"
		"\t-ps   <text>\tSet Programme Service Name (PSN)\n"
		"\t-p    <hex>\tSet Programme Identifier (PI)\n"
		"\t-pty  <int>\tSet Programme Type (PTY)\n"
		"\t-ptyn <text>\tSet Programme Type Name (PTYN)\n"
		"\t-ecc  <hex>\tSet Extended Country Code (ECC)\n"
		"\t-lic  <hex>\tSet Language Identifier Code\n");
}

/* Yes it's ugly... */
int
main(int argc, char *argv[])
{
	int rds_state_fd = 0;
	int ret = 0;
	int i = 0;
	uint16_t pi = 0;
	uint8_t pty = 0;
	uint8_t ecc = 0;
	uint16_t lic = 0;
	char temp[TEMP_BUF_LEN] = {0};
	struct rds_encoder_state *st = NULL;

	if(argc < 2)
		usage(argv[0]);

	rds_state_fd = shm_open(RDS_ENC_SHM_NAME, O_RDWR, 0600);
	if(rds_state_fd < 0) {
		ret = -5;
		goto cleanup;
	}

	ret = ftruncate(rds_state_fd, sizeof(struct rds_encoder));
	if(ret != 0) {
		ret = -5;
		goto cleanup;
	}


	st = (struct rds_encoder_state*)
				mmap(0, sizeof(struct rds_encoder_state),
				     PROT_READ | PROT_WRITE, MAP_SHARED,
				     rds_state_fd, 0);
	if(st == MAP_FAILED) {
		ret = -5;
		goto cleanup;
	}

	for(i = 1; i < argc; i++) {
		if(!strncmp(argv[i], "-g", 3)) {
			printf("Current config:\n"
			"\tStatus: %s\n"
			"\tPI:   0x%X\n"
			"\tECC:  0x%X\n"
			"\tLIC:  0x%X\n"
			"\tPTY:  %i\n"
			"\tPS:   %s\n"
			"\tRT:   %s\n"
			"\tPTYN: %s\n",
			st->enabled ? "Enabled" : "Disabled",
			rds_get_pi(st),
			rds_get_ecc(st),
			rds_get_lic(st),
			rds_get_pty(st),
			rds_get_ps(st),
			st->rt_set ? rds_get_rt(st) : "<Not set>",
			st->ptyn_set ? rds_get_ptyn(st) : "<Not set>");
		}
		if(!strncmp(argv[i], "-e", 3)) {
			st->enabled = 1;
			printf("RDS encoder enabled\n");
		}
		if(!strncmp(argv[i], "-d", 3)) {
			st->enabled = 0;
			printf("RDS encoder disabled\n");
		}
		if(!strncmp(argv[i], "-p", 3)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, 7, "%s", argv[++i]);
				pi = (uint16_t) strtol(temp, NULL, 16);
				ret = rds_set_pi(st, pi);
				if(ret < 0) {
					printf("Failed to set PI !\n");
					goto cleanup;
				} else
					printf("PI set:  \t0x%X\n",pi);
			} else {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-pty", 5)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, 4, "%s", argv[++i]);
				pty = (uint8_t) atoi(temp);
				ret = rds_set_pty(st, pty);
				if(ret < 0) {
					printf("Failed to set PTY !\n");
					goto cleanup;
				} else
					printf("PTY set:\t%i\n",pty);
			} else {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-ps", 4)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, RDS_PS_LENGTH + 1, "%s",
							argv[++i]);
				ret = rds_set_ps(st, temp);
				if(ret < 0) {
					printf("Failed to set PS !\n");
					goto cleanup;
				} else
					printf("PS set:  \t%s\n",temp);
			} else  {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-rt", 4)) {
			if(i < argc - 1) {
				memset(temp, 0, 64);
				snprintf(temp, RDS_RT_LENGTH + 1, "%s",
							argv[++i]);
				ret = rds_set_rt(st, temp, 1);
				if(ret < 0) {
					printf("Failed to set RT !\n");
					goto cleanup;
				} else
					printf("RT set:  \t%s\n",temp);
			} else  {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-ptyn", 6)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, RDS_PTYN_LENGTH + 1, "%s",
							argv[++i]);
				ret = rds_set_ptyn(st, temp);
				if(ret < 0) {
					printf("Failed to set PTYN !\n");
					goto cleanup;
				} else
					printf("PTYN set:\t%s\n",temp);
			} else  {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-ecc", 5)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, 5, "%s", argv[++i]);
				ecc = (uint8_t) strtol(temp, NULL, 16);
				ret = rds_set_ecc(st, ecc);
				if(ret < 0) {
					printf("Failed to set ECC !\n");
					goto cleanup;
				} else
					printf("ECC set:  \t0x%X\n",ecc);
			} else {
				usage(argv[0]);
				goto cleanup;
			}
		}
		if(!strncmp(argv[i], "-lic", 5)) {
			if(i < argc - 1) {
				memset(temp, 0, TEMP_BUF_LEN);
				snprintf(temp, 6, "%s", argv[++i]);
				lic = (uint16_t) strtol(temp, NULL, 16);
				lic &= 0xFFF;
				ret = rds_set_lic(st, lic);
				if(ret < 0) {
					printf("Failed to set LIC !\n");
					goto cleanup;
				} else
					printf("LIC set:  \t0x%X\n",lic);
			} else {
				usage(argv[0]);
				goto cleanup;
			}
		}
	}

cleanup:
	if(rds_state_fd > 0)
		close(rds_state_fd);
	if(st)
		munmap(st, sizeof(struct rds_encoder_state));
	return ret;
}
