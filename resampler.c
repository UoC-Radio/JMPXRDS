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
 * the main oscilator and do our processing at that sampling rate. Same
 * goes for RDS which operates at a much smaller sampling rate than audio.
 * After the processing is done we again need to downsample the result
 * (the MPX signal) to the sample rate of the sound card (jack's sample
 * rate), so that it can go out. That's the purpose of the resampler
 * implemented here.
 */

/* Upsample audio to the main oscilator's sampling rate */
int
resampler_upsample_audio(struct resampler_data *rsmpl, float *in_l, float *in_r,
						float *out_l, float *out_r,
						uint32_t inframes, uint32_t outframes)
{
	soxr_error_t error;
	size_t frames_used = 0;
	size_t frames_generated = 0;
	soxr_cbuf_t in[2];
	soxr_buf_t out[2];

	/* No need to upsample anything, just copy the buffers.
	 * Note: This is here for debugging mostly */
	if(rsmpl->audio_upsampler_bypass) {
		memcpy(out_l, in_l, inframes * sizeof(float));
		memcpy(out_r, in_r, inframes * sizeof(float));
		frames_generated = inframes;
	} else {

		in[0] = in_l;
		in[1] = in_r;

		out[0] = out_l;
		out[1] = out_r;
		error = soxr_process(rsmpl->audio_upsampler, in, inframes, &frames_used,
					out, outframes, &frames_generated);
	}

	if (error)
		return -1;
	else
		return frames_generated;
}

/* Upsample RDS waveform to the main oscilator's sampling rate */
int
resampler_upsample_rds(struct resampler_data *rsmpl, float *in, float *out,
						uint32_t inframes, uint32_t outframes)
{
	soxr_error_t error;
	size_t frames_used = 0;
	size_t frames_generated = 0;
	
	error = soxr_process(rsmpl->rds_upsampler, in, inframes, &frames_used,
					out, outframes, &frames_generated);
	if (error)
		return -1;
	else
		return frames_generated;
}

/* Downsample MPX signal to JACK's sample rate */
int
resampler_downsample_mpx(struct resampler_data *rsmpl, float *in, float *out,
						uint32_t inframes, uint32_t outframes)
{
	soxr_error_t error;
	size_t frames_used = 0;
	size_t frames_generated = 0;
	
	/* No need to upsample anything, just copy the buffers.
	 * Note: This is here for debugging mostly */
	if(rsmpl->mpx_downsampler_bypass) {
		memcpy(out, in, inframes * sizeof(float));
		frames_generated = inframes;
	} else {
		error = soxr_process(rsmpl->mpx_downsampler, in, inframes, &frames_used,
					out, outframes, &frames_generated);
	}

	if (error)
		return -1;
	else
		return frames_generated;
}


/****************\
* INIT / DESTROY *
\****************/

int
resampler_init(struct resampler_data *rsmpl, uint32_t jack_samplerate,
				uint32_t osc_samplerate,
				uint32_t rds_samplerate,
				uint32_t output_samplerate,
				uint32_t max_process_frames)
{
	int ret = 0;
	soxr_error_t error;
	soxr_io_spec_t io_spec;
	soxr_runtime_spec_t runtime_spec;
	soxr_quality_spec_t q_spec;

	if(rsmpl == NULL)
		return -1;

	memset(rsmpl, 0, sizeof(struct resampler_data));

	/* So that RDS encoder can calculate its buffer lengths */
	rsmpl->osc_samplerate = osc_samplerate;

	/* AUDIO UPSAMPLER */

	if (jack_samplerate == osc_samplerate) {
		rsmpl->audio_upsampler_bypass = 1;
		goto audio_upsampler_bypass;
	}

	/* Initialize upsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32_S, SOXR_FLOAT32_S);
	runtime_spec = soxr_runtime_spec(1);	/* TODO: OpenMP support */
	q_spec = soxr_quality_spec(SOXR_LQ, 0);
	/* 1 is Nyquist freq (half the sampling rate) so this rate is
	 * relative to the Nyquist freq */
	q_spec.passband_end = ((double) 16500 / (double) osc_samplerate) * 2.0L;
	q_spec.stopband_begin = ((double) 19000 / (double) osc_samplerate) * 2.0L;
	rsmpl->audio_upsampler = soxr_create(jack_samplerate, osc_samplerate, 2,
					&error, &io_spec, &q_spec, &runtime_spec);
	if(error){
		ret = -1;
		goto cleanup;
	}

 audio_upsampler_bypass:

	/* RDS UPSAMPLER */

	/* Initialize upsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32, SOXR_FLOAT32);
	q_spec = soxr_quality_spec(SOXR_LQ, 0);
	q_spec.passband_end = ((double)  16000 / (double) osc_samplerate) * 2.0L;
	q_spec.stopband_begin = ((double) (rds_samplerate / 2) / (double) osc_samplerate) * 2.0L;
	rsmpl->rds_upsampler = soxr_create(rds_samplerate, osc_samplerate, 1,
					&error, &io_spec, &q_spec, &runtime_spec);

	/* DOWNSAMPLER */

	if (osc_samplerate == output_samplerate) {
		rsmpl->mpx_downsampler_bypass = 1;
		goto cleanup;
	}

	/* Initialize downsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32, SOXR_FLOAT32);
	q_spec = soxr_quality_spec(SOXR_HQ, 0);
	q_spec.passband_end = ((double)   60000 / (double) output_samplerate) * 2.0L;

	rsmpl->mpx_downsampler = soxr_create(osc_samplerate, output_samplerate, 1,
					&error, &io_spec, &q_spec, &runtime_spec);

 cleanup:
	if (ret < 0)
		resampler_destroy(rsmpl);

	return ret;
}

void
resampler_destroy(struct resampler_data *rsmpl)
{
	soxr_delete(rsmpl->audio_upsampler);
	soxr_delete(rsmpl->rds_upsampler);
	soxr_delete(rsmpl->mpx_downsampler);
}

