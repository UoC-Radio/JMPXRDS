/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Resampler
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
#include "resampler.h"
#include <stdlib.h>	/* For NULL */
#include <stdio.h>	/* For printf */
#include <string.h>	/* For memset/memcpy */

/**************\
* ENTRY POINTS *
\**************/

/*
 * Since we oscilate the sound using high frequency signals from the
 * main oscilator, we need to upsample the sound to the sample rate of
 * the main oscilator and do our processing at that sampling rate.
 * After the processing is done we again need to downsample the result
 * (the MPX signal) to the sample rate of the sound card (jack's sample
 * rate), so that it can go out. That's the purpose of the resampler
 * implemented here.
 */

/* Upsample audio to the oscilator's sample rate */
float*
resampler_upsample_audio(struct resampler_data *rsmpl, float *in,
					uint32_t inframes, int *ret)
{
	uint32_t frames_generated = 0;

	/* No need to upsample anything, just copy the buffers.
	 * Note: This is here for debugging mostly */
	if(rsmpl->upsampler_ratio == 1.0) {
		memcpy(rsmpl->upsampled_audio, in, 2 * inframes *
						sizeof(float));
		frames_generated = 2 * inframes;
	} else {
		rsmpl->upsampler_data.data_in = in;
		rsmpl->upsampler_data.data_out = rsmpl->upsampled_audio;
		rsmpl->upsampler_data.input_frames = inframes;
		/* Note: by frames src means samples * channels
		 * so we divide here by num of channels (2) */
		rsmpl->upsampler_data.output_frames =
					rsmpl-> upsampled_audio_len / 2;
		rsmpl->upsampler_data.end_of_input = 0;
		rsmpl->upsampler_data.src_ratio = rsmpl->upsampler_ratio;
		*(ret) = src_process(rsmpl->upsampler_state,
					&rsmpl->upsampler_data);
		if(*(ret) != 0) {
			printf("UPSAMPLER: %s (%i)\n",
				src_strerror(*(ret)),
				*(ret));
			return NULL;
		}
		frames_generated = rsmpl->upsampler_data.output_frames_gen;
	}

	*(ret) = frames_generated;
	return rsmpl->upsampled_audio;
}

/* Downsample MPX signal to JACK's sample rate */
float*
resampler_downsample_mpx(struct resampler_data *rsmpl, float *in, float *out,
						uint32_t inframes, int *ret)
{
	int i = 0;
	uint32_t frames_generated = 0;

	if(rsmpl->downsampler_ratio == 1.0) {
		memcpy(out, in, inframes * sizeof(float));
		frames_generated = inframes;
	} else {
		/* Cut anything above the Niquist frequency to
		 * reduce noise on downsampler */
		for(i = 0; i < frames_generated; i++)
			in[i] = bessel_lp_apply(&rsmpl->mpx_lpf, in[i], 0);

		rsmpl->downsampler_data.data_in = in;
		rsmpl->downsampler_data.data_out = out;
		rsmpl->downsampler_data.input_frames = inframes;
		rsmpl->downsampler_data.output_frames = inframes;
		rsmpl->downsampler_data.end_of_input = 0;
		rsmpl->downsampler_data.src_ratio = rsmpl->downsampler_ratio;
		*(ret) = src_process(rsmpl->downsampler_state,
					&rsmpl->downsampler_data);
		if(*(ret) != 0) {
			printf("DOWNSAMPLER: %s (%i)\n",
				src_strerror(*(ret)),
				*(ret));
			return NULL;
		}
		frames_generated = rsmpl->downsampler_data.output_frames_gen;
	}

	*(ret) = frames_generated;
	return out;
}


/****************\
* INIT / DESTROY *
\****************/

int
resampler_init(struct resampler_data *rsmpl, uint32_t jack_samplerate,
				uint32_t osc_sample_rate,
				uint32_t output_samplerate,
				uint32_t max_process_frames)
{
	int ret = 0;
	uint32_t mpx_cutoff_freq = 0;

	/* We need to cut off everything above the Niquist frequency
	 * (half the sample rate) when downsampling or we might introduce
	 * distortion */
	bessel_lp_init(&rsmpl->mpx_lpf);

	/* Calculate resampling ratios */
	rsmpl->upsampler_ratio = (double) osc_sample_rate /
					(double) jack_samplerate;
	if(rsmpl->upsampler_ratio < 1)
		return -1;
	rsmpl->downsampler_ratio = (double) output_samplerate /
					(double) osc_sample_rate;

	/* Allocate buffers, note that for src frames mean
	 * number of samples * channels (so one frame has
	 * two samples in case of stereo audio) */
	rsmpl->upsampled_audio_len = rsmpl->upsampler_ratio *
			2 * max_process_frames * sizeof(float);
	rsmpl->upsampled_audio = (float*) malloc(rsmpl->upsampled_audio_len);
	memset(rsmpl->upsampled_audio, 0, rsmpl->upsampled_audio_len);

	/* Initialize upsampler/downsampler states,
	 * XXX: Maybe use a better -but more CPU hungry- resampler
	 * for downsampling, in my tests it was ok */
	rsmpl->upsampler_state = src_new(SRC_SINC_FASTEST, 2, &ret);
	if(ret != 0) {
		printf("UPSAMPLER: %s\n",src_strerror(ret));
		return -1;
	}
	memset(&rsmpl->upsampler_data, 0, sizeof(SRC_DATA));

	rsmpl->downsampler_state = src_new(SRC_SINC_FASTEST, 1, &ret);
	if(ret != 0) {
		printf("DOWNSAMPLER: %s\n",src_strerror(ret));
		return -1;
	}
	memset(&rsmpl->downsampler_data, 0, sizeof(SRC_DATA));

	return 0;
}

void
resampler_destroy(struct resampler_data *rsmpl)
{
	if(rsmpl->upsampled_audio != NULL)
		free(rsmpl->upsampled_audio);
	if(rsmpl->upsampler_state != NULL)
		src_delete(rsmpl->upsampler_state);
	if(rsmpl->downsampler_state != NULL)
		src_delete(rsmpl->downsampler_state);
}

