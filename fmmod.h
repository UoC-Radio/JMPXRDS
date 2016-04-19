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
#include "oscilator.h"  /* Also brings in stdint.h and config.h */
#include "rds_encoder.h" /* Also brings in stdint.h, resampler.h and jack/jack.h */
#include "filters.h"    /* Also brings in stdint.h */

/* We need something big enough to output the MPX
 * signal. 96KHz should be enough for the audio part
 * with SSB enabled but for proper stereo imaging and
 * in order to support RDS output too (57Khz carrier)
 * 192Khz is needed. */
#define FMMOD_OUTPUT_SAMPLERATE_MIN	192000

enum fmmod_errors {
	FMMOD_ERR_INVALID_INPUT = -1,
	FMMOD_ERR_RESAMPLER_ERR	= -2,
	FMMOD_ERR_JACKD_ERR = -3,
	FMMOD_ERR_NOMEM = -4,
	FMMOD_ERR_OSC_ERR = -5,
	FMMOD_ERR_RDS_ERR = -6,
	FMMOD_ERR_SHM_ERR = -7,
	FMMOD_ERR_SOCK_ERR = -8,
};

/* If JACK's samplerate is not enough (e.g. soundcard
 * can't do 192KHz or CPU can't handle 192KHz processsing)
 * output data to a socket instead so that it can still be
 * transmitted from another host or another soundcard. */
enum fmmod_output {
	FMMOD_OUT_JACK = 1,
	FMMOD_OUT_SOCK = 2
};

/* FM pre-emphasis filter parameters (tau)
 * are different for EU and US */
enum fmmod_region {
	FMMOD_REGION_US = 0,
	FMMOD_REGION_EU = 1,
	FMMOD_REGION_WORLD = 2
};

/* Stereo signal (L-R) encoding:
 * DSB -> Double side band (default)
 * SSB HARTLEY -> Single Side Band Hartley modulator
 * SSB WEAVER -> Single Side Band Weaver modulator
 * For more infos check out fmmod.c */
enum fmmod_stereo_modulation {
	FMMOD_DSB = 0,
	FMMOD_SSB_HARTLEY = 1,
	FMMOD_SSB_WEAVER = 2
};

/* Control I/O channel */
struct fmmod_control {
	float	audio_gain;
	float	pilot_gain;
	float	rds_gain;
	float	ssb_carrier_gain;
	float	mpx_gain;
	int	stereo_modulation;
	int	use_audio_lpf;
	float	peak_mpx_out;
	float	peak_audio_in_l;
	float	peak_audio_in_r;
};

#define FMMOD_CTL_SHM_NAME "FMMOD_CTL_SHM"

struct fmmod_instance {
	/* State */
	int active;
	/* Audio input buffer */
	float *inbuf_l;
	float *inbuf_r;
	uint32_t inbuf_len;
	/* Upsampled audio buffer */
	float *uaudio_buf_l;
	float *uaudio_buf_r;
	uint32_t uaudio_buf_len;
	/* MPX Output buffer */
	float *mpxbuf;
	uint32_t mpxbuf_len;
	int output_type;
	/* For socket output */
	float *sock_outbuf;
	uint32_t sock_outbuf_len;
	int out_sock_fd;
	/* The Oscilator */
	struct osc_state sin_osc;
	/* The audio filter */
	struct audio_filter aflt;
	/* The resampler */
	struct resampler_data rsmpl;
	/* The RDS Encoder */
	struct rds_encoder rds_enc;
	/* Jack-related */
	jack_port_t *inL;
	jack_port_t *inR;
	jack_port_t *outMPX;
	jack_client_t *client;
	jack_nframes_t added_latency;
	/* SSB modulators */
	struct osc_state cos_osc;
	struct ssb_filter_data ssb_lpf;
	struct hilbert_transformer_data ht;
	/* Control */
	struct fmmod_control *ctl;
};

typedef float (*stereo_modulator)(struct fmmod_instance *, float);

int fmmod_initialize(struct fmmod_instance *fmmod, int region);
void fmmod_destroy(struct fmmod_instance *fmmod, int shutdown);

