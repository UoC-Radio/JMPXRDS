/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - FMMOD runtime configuration tool
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
#include "utils.h"
#include "fmmod.h"
#include <stdlib.h>		/* For strtol */
#include <stdio.h>		/* For snprintf */
#include <string.h>		/* For memset / strncmp */
#include <unistd.h>		/* For getopt() */

#define TEMP_BUF_LEN	3 + 1

void
usage(char *name)
{
	utils_ann("FMMOD Configuration tool for JMPXRDS\n");
	utils_info("Usage: %s -g or [<parameter> <value>] pairs\n", name);
	utils_info("\nParameters:\n"
		"\t-g\t\tGet current values\n"
		"\t-a   <int>\tSet audio gain precentage (default is 40%%)\n"
		"\t-m   <int>\tSet MPX gain percentage (default is 100%%)\n"
		"\t-p   <int>\tSet pilot gain percentage (default is 8%%)\n"
		"\t-r   <int>\tSet RDS gain percentage (default is 2%%)\n"
		"\t-c   <int>\tSet stereo carrier gain percentage (default is 100%%)\n"
		"\t-s   <int>\tSet stereo mode 0-> DSBSC (default), 1-> SSB (Hartley),\n"
				"\t\t\t\t\t2-> SSB (LP Filter), 3-> Mono\n"
		"\t-f   <int>\tEnable Audio LPF (FIR) (1 -> enabled (default), 0-> disabled)\n"
		"\t-e	<int>\tSet FM Pre-emphasis tau (0-> 50us, 1-> 75us, 2-> Disabled)\n");
}


int
main(int argc, char *argv[])
{
	int ret = 0;
	int opt = 0;
	char temp[TEMP_BUF_LEN] = { 0 };
	struct shm_mapping *shmem = NULL;
	struct fmmod_control *ctl = NULL;

	shmem = utils_shm_attach(FMMOD_CTL_SHM_NAME,
				 sizeof(struct fmmod_control));
	if (!shmem) {
		utils_perr("Unable to communicate with JMPXRDS");
		return -1;
	}
	ctl = (struct fmmod_control*) shmem->mem;

	while ((opt = getopt(argc, argv, "ga:m:p:r:c:s:f:e:")) != -1)
		switch (opt) {
		case 'g':
			utils_info("Current config:\n"
				"\tAudio:     %i%%\n"
				"\tMPX:       %i%%\n"
				"\tPilot:     %i%%\n"
				"\tRDS:       %i%%\n"
				"\tStereo gain: %i%%\n"
				"\tStereo mode: %s\n"
				"\tAudio LPF: %s\n"
				"\tFM Pre-emph tau: %s\n"
				"Current gains:\n"
				"\tAudio Left:  %f\n"
				"\tAudio Right: %f\n"
				"\tMPX:         %f\n",
				(int)(100 * ctl->audio_gain),
				(int)(100 * ctl->mpx_gain),
				(int)(100 * ctl->pilot_gain),
				(int)(100 * ctl->rds_gain),
				(int)(100 * ctl->stereo_carrier_gain),
				ctl->stereo_modulation == FMMOD_MONO ? "Mono" :
				ctl->stereo_modulation ==
					FMMOD_SSB_HARTLEY ? "SSB (Hartley)" :
				ctl->stereo_modulation ==
					FMMOD_SSB_LPF ? "SSB (LP Filter)" :
				"DSBSC",
				ctl->use_audio_lpf ? "Enabled" : "Disabled",
				(ctl->preemph_tau == 0) ? "50us (World)" :
				(ctl->preemph_tau == 1) ? "75us (U.S.A.)" :
				"Disabled",
				ctl->peak_audio_in_l, ctl->peak_audio_in_r,
				ctl->peak_mpx_out);
			break;

		case 'a':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			ctl->audio_gain = (float)(strtol(temp, NULL, 10)) / 100.0;
			utils_info("New audio gain:  \t%i%%\n",
				   (int)(100 * ctl->audio_gain));
			break;

		case 'm':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			ctl->mpx_gain = (float)(strtol(temp, NULL, 10)) / 100.0;
			utils_info("New MPX gain:  \t%i%%\n",
				   (int)(100 * ctl->mpx_gain));
			break;

		case 'p':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			ctl->pilot_gain = (float)(strtol(temp, NULL, 10)) / 100.0;
			utils_info("New pilot gain:  \t%i%%\n",
				   (int)(100 * ctl->pilot_gain));
			break;

		case 'r':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			ctl->rds_gain = (float)(strtol(temp, NULL, 10)) / 100.0;
			utils_info("New RDS gain:  \t%i%%\n",
				   (int)(100 * ctl->rds_gain));
			break;

		case 'c':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 4, "%s", optarg);
			ctl->stereo_carrier_gain =
				(float)(strtol(temp, NULL, 10)) / 100.0;
			utils_info("New stereo carrier gain:  \t%i%%\n",
				   (int)(100 * ctl->stereo_carrier_gain));
			break;

		case 's':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 2, "%s", optarg);
			ctl->stereo_modulation = strtol(temp, NULL, 10) & 0x7;
			if(ctl->stereo_modulation > FMMOD_MONO)
				ctl->stereo_modulation = FMMOD_DSB;
			utils_info("Set stereo modulation:  \t%i\n",
				   ctl->stereo_modulation);
			break;

		case 'f':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 2, "%s", optarg);
			ctl->use_audio_lpf = strtol(temp, NULL, 10) & 0x1;
			utils_info("Set Audio LPF status:  \t%i\n",
				   ctl->use_audio_lpf);
			break;
		case 'e':
			memset(temp, 0, TEMP_BUF_LEN);
			snprintf(temp, 2, "%s", optarg);
			ret = strtol(temp, NULL, 10) & 0x3;
			if(ret == 0 || ret >= LPF_PREEMPH_MAX)
				ctl->preemph_tau = LPF_PREEMPH_50US;
			else if(ret == 1)
				ctl->preemph_tau = LPF_PREEMPH_75US;
			else
				ctl->preemph_tau = LPF_PREEMPH_NONE;
			utils_info("Set FM Pre-emphasis tau:  \t%i\n",
				   ctl->preemph_tau);
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
