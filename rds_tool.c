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
#include "utils.h"
#include <stdlib.h>		/* For atoi / strtol */
#include <stdio.h>		/* For snprintf */
#include <string.h>		/* For memset / strnlen / strncmp */
#include <getopt.h>		/* For getopt_long_only() */

#define TEMP_BUF_LEN	RDS_RT_LENGTH + 1
void usage(char *name)
{
	utils_ann("RDS Configuration tool for JMPXRDS\n");
	utils_info("Usage: %s -g or [<parameter> <value>] pairs\n", name);
	utils_info("\nParameters:\n"
	       "\t-g\t\tGet current config\n"
	       "\t-e          \tEnable RDS encoder\n"
	       "\t-d          \tDisable RDS encoder\n"
	       "\t-rt   <text>\tSet radiotext\n"
	       "\t-ps   <text>\tSet Programme Service Name (PSN)\n"
	       "\t-pi   <hex>\tSet Programme Identifier (PI)\n"
	       "\t-pty  <int>\tSet Programme Type (PTY)\n"
	       "\t-ptyn <text>\tSet Programme Type Name (PTYN)\n"
	       "\t-ecc  <hex>\tSet Extended Country Code (ECC)\n"
	       "\t-lic  <hex>\tSet Language Identifier Code\n");
}

static const struct option opts[] = {
	{"rt",	required_argument,0,	1},
	{"ps",	required_argument,0,	2},
	{"pi",	required_argument,0,	3},
	{"pty",	required_argument,0,	4},
	{"ptyn", required_argument,0,	5},
	{"ecc",	required_argument,0,	6},
	{"lic",	required_argument,0,	7},
	{0,	0,		0,	0}
};

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
	char temp[TEMP_BUF_LEN] = { 0 };
	struct shm_mapping *shmem = NULL;
	struct rds_encoder_state *st = NULL;
	int opt = 0;
	int opt_idx = 0;

	shmem = utils_shm_attach(RDS_ENC_SHM_NAME,
				 sizeof(struct rds_encoder_state));
	if (!shmem) {
		utils_perr("Unable to communicate with the RDS encoder");
		return -1;
	}
	st = (struct rds_encoder_state*) shmem->mem;


	while ((opt = getopt_long_only(argc, argv,"ged", opts, &opt_idx)) != -1) {
		switch(opt) {
		case 'g':
			utils_info("Current config:\n"
				"\tStatus: %s\n"
				"\tPI:   0x%X\n"
				"\tECC:  0x%X\n"
				"\tLIC:  0x%X\n"
				"\tPTY:  %i\n"
				"\tPSN:   %s\n"
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
			break;
		case 'e':
			st->enabled = 1;
			utils_info("RDS encoder enabled\n");
			break;
		case 'd':
			st->enabled = 0;
			utils_info("RDS encoder disabled\n");
			break;
		case 1:	/* RadioText */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, RDS_RT_LENGTH + 1, "%s", optarg);
			ret = rds_set_rt(st, temp, 1);
			if (ret < 0) {
				utils_err("Failed to set RT !\n");
				goto cleanup;
			} else
				utils_info("RT set:  \t%s\n", temp);
			break;
		case 2:	/* Programme Service Name */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, RDS_PS_LENGTH + 1, "%s", optarg);
			ret = rds_set_ps(st, temp);
			if (ret < 0) {
				utils_err("Failed to set PS !\n");
				goto cleanup;
			} else
				utils_info("PS set:  \t%s\n", temp);
			break;
		case 3:	/* Programme Identifier */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 7, "%s", optarg);
			pi = (uint16_t) strtol(temp, NULL, 16);
			ret = rds_set_pi(st, pi);
			if (ret < 0) {
				utils_err("Failed to set PI !\n");
				goto cleanup;
			} else
				utils_info("PI set:  \t0x%X\n", pi);
			break;
		case 4:	/* Programme Type */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			pty = (uint8_t) atoi(temp);
			ret = rds_set_pty(st, pty);
			if (ret < 0) {
				utils_err("Failed to set PTY !\n");
				goto cleanup;
			} else
				utils_info("PTY set:\t%i\n", pty);
			break;
		case 5:	/* Programme Type Name */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, RDS_PTYN_LENGTH + 1, "%s", optarg);
			ret = rds_set_ptyn(st, temp);
			if (ret < 0) {
				utils_err("Failed to set PTYN !\n");
				goto cleanup;
			} else
				utils_info("PTYN set:\t%s\n", temp);
			break;
		case 6:	/* Extended Country Code */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 5, "%s", optarg);
			ecc = (uint8_t) strtol(temp, NULL, 16);
			ret = rds_set_ecc(st, ecc);
			if (ret < 0) {
				utils_err("Failed to set ECC !\n");
				goto cleanup;
			} else
				utils_info("ECC set:  \t0x%X\n", ecc);
			break;
		case 7:	/* Language Identifier Code */
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 6, "%s", optarg);
			lic = (uint16_t) strtol(temp, NULL, 16);
			lic &= 0xFFF;
			ret = rds_set_lic(st, lic);
			if (ret < 0) {
				utils_err("Failed to set LIC !\n");
				goto cleanup;
			} else
				utils_info("LIC set:  \t0x%X\n", lic);
			break;
		default:
			usage(argv[0]);
			utils_shm_destroy(shmem, 0);
			return -1;
		}
	}

	if (argc < 2 || (argc > 1 && optind == 1)) {
		usage(argv[0]);
		ret = -1;
	}

 cleanup:
	utils_shm_destroy(shmem, 0);
	return ret;
}
