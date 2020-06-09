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
#include "utils.h"
#include <stdlib.h>		/* For NULL */
#include <string.h>		/* For memset/memcpy */
#include <jack/thread.h>	/* For thread handling through jack */


/*********\
* HELPERS *
\*********/

static void
resampler_thread_run(struct resampler_thread_data *rstd)
{
	rstd->result = soxr_process(rstd->resampler, &rstd->in, rstd->inframes,
				    &rstd->frames_used, &rstd->out, rstd->outframes,
				    &rstd->frames_generated);
}

#ifdef JMPXRDS_MT
static void*
resampler_loop(void *arg)
{
	struct resampler_thread_data *rstd = (struct resampler_thread_data*) arg;

	while((*rstd->active)) {
		pthread_mutex_lock(&rstd->proc_mutex);
		while (pthread_cond_wait(&rstd->proc_trigger, &rstd->proc_mutex) != 0);

		if(!(*rstd->active))
			break;

		resampler_thread_run(rstd);

		pthread_mutex_unlock(&rstd->proc_mutex);

		/* Let the caller know we are done */
		pthread_mutex_lock(&rstd->done_mutex);
		pthread_cond_signal(&rstd->done_trigger);
		pthread_mutex_unlock(&rstd->done_mutex);
	}

	return arg;
}
#endif

static int
resampler_init_upsampler_threads(struct resampler_data *rsmpl)
{
	struct resampler_thread_data *rstd_l = &rsmpl->rstd_l;
	struct resampler_thread_data *rstd_r = &rsmpl->rstd_r;
#ifdef JMPXRDS_MT
	int ret = 0;
#endif

	rstd_l->resampler = rsmpl->audio_upsampler_l;
	rstd_l->active = &rsmpl->active;

	rstd_r->resampler = rsmpl->audio_upsampler_r;
	rstd_r->active = &rsmpl->active;

#ifdef JMPXRDS_MT
	pthread_mutex_init(&rstd_l->proc_mutex, NULL);
	pthread_cond_init(&rstd_l->proc_trigger, NULL);
	pthread_mutex_init(&rstd_l->done_mutex, NULL);
	pthread_cond_init(&rstd_l->done_trigger, NULL);

	ret = jack_client_create_thread(rsmpl->fmmod_client, &rstd_l->tid,
					jack_client_real_time_priority(rsmpl->fmmod_client),
					jack_is_realtime(rsmpl->fmmod_client),
					resampler_loop, (void *) rstd_l);
	if(ret < 0) {
		utils_err("[JACKD] Could not create processing thread\n");
		return -1;
	}
#endif

	return 0;
}


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
resampler_upsample_audio(struct resampler_data *rsmpl,
			 const float *in_l, const float *in_r,
			 float *out_l, float *out_r,
			 uint32_t inframes, uint32_t outframes)
{
	struct resampler_thread_data *rstd_l = &rsmpl->rstd_l;
	struct resampler_thread_data *rstd_r = &rsmpl->rstd_r;
	size_t frames_generated = 0;

	/* No need to upsample anything, just copy the buffers.
	 * Note: This is here for debugging mostly */
	if (rsmpl->audio_upsampler_bypass) {
		memcpy(out_l, in_l, inframes * sizeof(float));
		memcpy(out_r, in_r, inframes * sizeof(float));
		frames_generated = inframes;
		return frames_generated;
	}

#ifdef JMPXRDS_MT
	pthread_mutex_lock(&rstd_l->proc_mutex);
	rstd_l->inframes = inframes;
	rstd_l->in = in_l;
	rstd_l->out = out_l;
	rstd_l->outframes = outframes;
	pthread_mutex_unlock(&rstd_l->proc_mutex);

	rstd_r->inframes = inframes;
	rstd_r->in = in_r;
	rstd_r->out = out_r;
	rstd_r->outframes = outframes;

	/* Signal the left channel thread to start
	 * processing this chunk */
	pthread_mutex_lock(&rstd_l->proc_mutex);
	pthread_cond_signal(&rstd_l->proc_trigger);
	pthread_mutex_unlock(&rstd_l->proc_mutex);

	/* Process right channel on current thread */
	resampler_thread_run(rstd_r);

	/* Wait for the left channel thread to finish */
	while(pthread_cond_wait(&rstd_l->done_trigger, &rstd_l->done_mutex) != 0);

#else
	rstd_l->inframes = inframes;
	rstd_l->in = in_l;
	rstd_l->out = out_l;
	rstd_l->outframes = outframes;

	resampler_thread_run(rstd_l);

	rstd_r->inframes = inframes;
	rstd_r->in = in_r;
	rstd_r->out = out_r;
	rstd_r->outframes = outframes;

	resampler_thread_run(rstd_r);
#endif
	if(rstd_l->result || rstd_r->result) {
		utils_err("[RESAMPLER] Audio upsampling failed on this period: %i (L), %i (R)\n",
			  rstd_l->result, rstd_r->result);
		return -1;
	}

	if (rstd_l->frames_generated == 0)
		utils_wrn("[RESAMPLER] Audio upsampler didn't generate any frames\n");

	return rstd_l->frames_generated;
}

/* Upsample RDS waveform to the main oscilator's sampling rate */
int
resampler_upsample_rds(const struct resampler_data *rsmpl, const float *in, float *out,
		       uint32_t inframes, uint32_t outframes)
{
	soxr_error_t error;
	size_t frames_used = 0;
	size_t frames_generated = 0;

	error = soxr_process(rsmpl->rds_upsampler, in, inframes, &frames_used,
			     out, outframes, &frames_generated);
	if (error) {
		utils_err("[RESAMPLER] RDS upsampling failed on this period: %i\n",
			  error);
		return -1;
	}

	if (frames_generated == 0)
		utils_wrn("[RESAMPLER] RDS upsampler didn't generate any frames\n");

	return frames_generated;
}

/* Downsample MPX signal to JACK's sample rate */
int
resampler_downsample_mpx(const struct resampler_data *rsmpl, const float *in, float *out,
			 uint32_t inframes, uint32_t outframes)
{
	soxr_error_t error;
	size_t frames_used = 0;
	size_t frames_generated = 0;

	/* No need to upsample anything, just copy the buffers.
	 * Note: This is here for debugging mostly */
	if (rsmpl->mpx_downsampler_bypass) {
		memcpy(out, in, inframes * sizeof(float));
		frames_generated = inframes;
		return frames_generated;
	} else {
		error = soxr_process(rsmpl->mpx_downsampler, in, inframes,
				     &frames_used, out, outframes,
				     &frames_generated);
	}

	if (error) {
		utils_err("[RESAMPLER] MPX downsampling failed on this period: %i\n",
			  error);
		return -1;
	}

	if (frames_generated == 0)
		utils_wrn("[RESAMPLER] MPX downsampler didn't generate any frames\n");

	return frames_generated;
}

/****************\
* INIT / DESTROY *
\****************/

int
resampler_init(struct resampler_data *rsmpl, uint32_t jack_samplerate,
		jack_client_t *fmmod_client, uint32_t osc_samplerate,
		uint32_t rds_samplerate, uint32_t output_samplerate)
{
	soxr_error_t error;
	soxr_io_spec_t io_spec;
	soxr_runtime_spec_t runtime_spec;
	soxr_quality_spec_t q_spec;
	int ret = 0;

	if (rsmpl == NULL)
		return -1;

	memset(rsmpl, 0, sizeof(struct resampler_data));

	/* So that RDS encoder can calculate its buffer lengths */
	rsmpl->osc_samplerate = osc_samplerate;

	rsmpl->fmmod_client = fmmod_client;

	/* AUDIO UPSAMPLER */

	if (jack_samplerate == osc_samplerate) {
		rsmpl->audio_upsampler_bypass = 1;
		utils_dbg("[RESAMPLER] Audio upsampler bypass !\n");
		goto audio_upsampler_bypass;
	}

	/* Initialize upsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32_S, SOXR_FLOAT32_S);
	runtime_spec = soxr_runtime_spec(1);
	q_spec = soxr_quality_spec(SOXR_QQ, 0);

	rsmpl->audio_upsampler_l = soxr_create(jack_samplerate, osc_samplerate, 1,
						&error, &io_spec, &q_spec,
						&runtime_spec);
	if (error) {
		utils_err("[RESAMPLER] Audio upsampler (L) init failed with code: %i\n",
			  error);
		ret = -2;
		goto cleanup;
	}

	rsmpl->audio_upsampler_r = soxr_create(jack_samplerate, osc_samplerate, 1,
						&error, &io_spec, &q_spec,
						&runtime_spec);
	if (error) {
		utils_err("[RESAMPLER] Audio upsampler (R) init failed with code: %i\n",
			  error);
		ret = -3;
		goto cleanup;
	}

	rsmpl->active = 1;
	ret = resampler_init_upsampler_threads(rsmpl);
	if (ret < 0) {
		utils_err("[RESAMPLER] Audio upsampler threads init failed\n");
		rsmpl->active = 0;
		ret = -4;
		goto cleanup;
	}

 audio_upsampler_bypass:

	/* RDS UPSAMPLER */

	/* Initialize upsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	runtime_spec = soxr_runtime_spec(1);
	q_spec = soxr_quality_spec(SOXR_QQ, 0);
	rsmpl->rds_upsampler = soxr_create(rds_samplerate, osc_samplerate, 1,
					   &error, &io_spec, &q_spec,
					   &runtime_spec);

	if (error) {
		utils_err("[RESAMPLER] RDS upsampler init failed with code: %i\n",
			  error);
		ret = -5;
		goto cleanup;
	}

	/* DOWNSAMPLER */

	if (osc_samplerate == output_samplerate) {
		rsmpl->mpx_downsampler_bypass = 1;
		goto cleanup;
	}

	/* Initialize downsampler's parameters */
	io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
	runtime_spec = soxr_runtime_spec(1);
	q_spec = soxr_quality_spec(SOXR_HQ, 0);
	q_spec.passband_end = ((double)60000 / (double)output_samplerate) * 2.0L;
	q_spec.stopband_begin = ((double)62500 / (double)output_samplerate) * 2.0L;

	rsmpl->mpx_downsampler = soxr_create(osc_samplerate, output_samplerate,
					     1, &error, &io_spec, &q_spec,
					     &runtime_spec);

	if (error) {
		utils_err("[RESAMPLER] MPX downsampler (L) init failed with code: %i\n",
			  error);
		ret = -6;
		goto cleanup;
	}

 cleanup:
	if (ret < 0) {
		utils_err("[RESAMPLER] Init failed with code: %i\n", ret);
		resampler_destroy(rsmpl);
	} else
		utils_dbg("[RESAMPLER] Init complete\n");

	return ret;
}

void
resampler_destroy(struct resampler_data *rsmpl)
{
	rsmpl->active = 0;
	/* SoXr checks if they are NULL or not */
	soxr_delete(rsmpl->audio_upsampler_l);
	soxr_delete(rsmpl->audio_upsampler_r);
	soxr_delete(rsmpl->rds_upsampler);
	soxr_delete(rsmpl->mpx_downsampler);
	utils_dbg("[RESAMPLER] Destroyed\n");
}
