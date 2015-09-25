/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Main processor
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

#include <jack/jack.h>
#include "oscilator.h"
#include "resampler.h" /* Also brings in filters.h */
#include "rds_encoder.h"

enum fmmod_errors {
	FMMOD_ERR_INVALID_INPUT = -1,
	FMMOD_ERR_RESAMPLER_ERR	= -2,
	FMMOD_ERR_JACKD_ERR = -3,
	FMMOD_ERR_NOMEM = -4,
	FMMOD_ERR_OSC_ERR = -5,
	FMMOD_ERR_RDS_ERR = -6,
	FMMOD_ERR_SHM_ERR = -7
};

struct fmmod_instance {
	float *ioaudiobuf;
	int ioaudiobuf_len;
	float *mpxbuf;
	int mpxbuf_len;
	struct resampler_data rsmpl;
	struct audio_filter aflt;
	struct osc_state sin_osc;
	jack_port_t *inL;
	jack_port_t *inR;
	jack_port_t *outMPX;
	jack_client_t *client;
	jack_nframes_t added_latency;
	/* SSB */
	struct osc_state cos_osc;
	struct ssb_filter_data ssb_lpf;
	struct hilbert_transformer_data ht;
	/* Parameters */
	int gain_audio;
	int gain_pilot;
	int enable_ssb;
	/* RDS Encoder */
	struct rds_encoder *enc;
};

#define REGION_US	0
#define REGION_EU	1
#define	REGION_WORLD	2

int fmmod_initialize(struct fmmod_instance *fmmod, int region);
void fmmod_destroy(struct fmmod_instance *fmmod);

