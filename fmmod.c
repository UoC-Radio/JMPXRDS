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
#ifndef _GNU_SOURCE		/* Defined by default when using g++ */
#define _GNU_SOURCE		/* F_SETPIPE_Z is Linux specific */
#endif
#include "utils.h"
#include "fmmod.h"
#include <jack/transport.h>
#include <jack/thread.h>	/* For thread handling through jack */
#include <stdlib.h>		/* For malloc() */
#include <unistd.h>		/* For ftruncate(), close() */
#include <string.h>		/* For memset() */
#include <stdio.h>		/* For snprintf */
#include <sys/mman.h>		/* For shm_open */
#include <sys/stat.h>		/* For mode constants */
#include <fcntl.h>		/* For O_* and F_* constants */
#include <errno.h>		/* For errno and EEXIST */
#include <math.h>		/* For fmin/fmax */

/*********\
* HELPERS *
\*********/

static inline int
num_resampled_samples(int in_srate, int out_srate, int num_samples)
{
	double ratio = (double) (out_srate) / (double) (in_srate);
	double olenf = ratio * ((double) num_samples);
	/* Also cover the case where out_srate < in_srate */
	olenf = fmax(olenf, num_samples - olenf);
	return (int) olenf;
}


static int
write_to_sock(struct fmmod_instance *fmmod, const float *samples, int num_samples)
{
	char sock_path[32] = { 0 };
	int uid = 0;
	int ret = 0;

	/* Socket not open yet */
	if (fmmod->out_sock_fd == 0) {
		uid = getuid();
		snprintf(sock_path, 32, "/run/user/%u/jmpxrds.sock", uid);

		fmmod->out_sock_fd = open(sock_path, O_WRONLY | O_NONBLOCK);
		if (fmmod->out_sock_fd < 0) {
			fmmod->out_sock_fd = 0;
			return 0;
		}
	}

	ret = write(fmmod->out_sock_fd, samples, num_samples * sizeof(float));
	if (ret < 0) {
		/* Pipe has broken -the other side closed the socket-
		 * close the descriptor and leave open fail until
		 * someone else opens up the socket again */
		if (errno == EPIPE) {
			close(fmmod->out_sock_fd);
			fmmod->out_sock_fd = 0;
			return 0;
		}
		utils_perr("[FMMOD] write() failed on socket");
	}

	return 0;
}


/************************\
* FM MPX STEREO ENCODING *
\************************/

/*
 * Mono generator, just L+R plus RDS if
 * available
 */
static int
fmmod_mono_generator(struct fmmod_instance *fmmod, const float* lpr,
		     __attribute__((unused)) const float* lmr,
		     int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	const struct fmmod_control *ctl = fmmod->ctl;
	int i = 0;

	/* No stereo pilot / subcarrier */
	for(i = 0; i < num_samples; i++) {
		/* L + R */
		out[i] = lpr[i];

		/* RDS symbols modulated by the 57KHz carrier (3 x Pilot) */
		out[i] += ctl->rds_gain * osc_get_57Khz_sample(sin_osc) *
					  rds_get_next_sample(&fmmod->rds_enc);

		/* Set mpx gain percentage */
		out[i] *= ctl->mpx_gain;

		osc_increase_phase(sin_osc);
	}

	return 0;
}


/*
 * Standard Double Sideband with Suppressed Carrier (DSBSC)
 * The input sample is AM modulated with a sine wave
 * at 38KHz (twice the pilot's frequency)
 */
static int
fmmod_dsb_generator(struct fmmod_instance *fmmod, const float* lpr,
		    const float* lmr, int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	const struct fmmod_control *ctl = fmmod->ctl;
	int i = 0;

	for(i = 0; i < num_samples; i++) {
		/* L + R */
		out[i] = lpr[i];

		/* Stereo Pilot at 19KHz */
		out[i] += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

		/* AM modulated L - R */
		out[i] += lmr[i] * osc_get_38Khz_sample(sin_osc) *
			           ctl->stereo_carrier_gain;

		/* RDS symbols modulated by the 57KHz carrier (3 x Pilot) */
		out[i] += ctl->rds_gain * osc_get_57Khz_sample(sin_osc) *
					  rds_get_next_sample(&fmmod->rds_enc);

		/* Set mpx gain percentage */
		out[i] *= ctl->mpx_gain;

		osc_increase_phase(sin_osc);
	}

	return 0;
}

/*
 * Single Side Band modulation
 *
 * Some notes on SSB modulation for the stereo subcarrier
 * http://ham-radio.com/k6sti/ssb.htm
 * http://wheatstone.com/index.php/corporate-support/all-downloads/
 * doc_download/502-new-findings-on-fm-stereo-multipath-control
 *
 * In my tests with both SSB modulators I got 6 - 8dB stereo
 * separation from my receiver (it doesn't have to do with the
 * modulation method it's the receiver that reduced the stereo
 * separation). I did get better RDS reception with RDS Spy and
 * better coverage though. It seems that many receivers will
 * misbehave so don't use SSB because it's "fancy". It's better
 * than mono but the standard subcarrier performs better.
 */

/****************************\
* FILTER BASED SSB MODULATOR *
\****************************/

/*
 * A simple FFT low pass filter that cuts off anything above
 * the carrier (the upper side band).
 */
static int
fmmod_ssb_lpf_generator(struct fmmod_instance *fmmod, const float* lpr,
			const float* lmr, int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	const struct fmmod_control *ctl = fmmod->ctl;
	const struct fmmod_flts *flts = &fmmod->flts;
	double saved_phase = 0.0L;
	int i = 0;

	/* If we apply the filter only on L-R to suppress its USB, we'll
	 * delay L-R by SSB_LPF_OVERLAP_FACTOR * num_samples and then we'll
	 * have to also delay L+R by the same amount through a delay buffer
	 * which is messy. Instead apply the filter on the combined (L+R) +
	 * (L-R), the filter will only cut the USB of L-R, leaving L+R
	 * unaffected. We'll re-use the output buffer for the filter. */

	/* Add L+R to the output buffer */
	for(i = 0; i < num_samples; i++) {
		out[i] = lpr[i];
	}

	/* Add the modulated L-R, save the oscilator's phase
	 * so that we can re-set it for calculating the pilot
	 * and the RDS parts */
	saved_phase = sin_osc->current_phase;
	for(i = 0; i < num_samples; i++) {
		out[i] += lmr[i] * osc_get_38Khz_sample(sin_osc);
		osc_increase_phase(sin_osc);
	}
	sin_osc->current_phase = saved_phase;

	/* Apply the lpf filter to suppres the USB of L-R */
	lpf_filter_apply(&flts->ssb_lpf, out, out,
		    num_samples, ctl->stereo_carrier_gain * 2.0);

	/* Now add the rest */
	for(i = 0; i < num_samples; i++) {

		/* Stereo Pilot at 19KHz */
		out[i] += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

		/* RDS symbols modulated by the 57KHz carrier (3 x Pilot) */
		out[i] += ctl->rds_gain * osc_get_57Khz_sample(sin_osc) *
					  rds_get_next_sample(&fmmod->rds_enc);

		/* Set mpx gain percentage */
		out[i] *= ctl->mpx_gain;

		osc_increase_phase(sin_osc);
	}

	return 0;
}


/*************************\
* HARTLEY MODULATOR (SSB) *
\*************************/

/*
 * For more information on the Hartley SSB modulator visit
 * http://dp.nonoo.hu/projects/ham-dsp-tutorial/09-ssb-hartley/
 */
static int
fmmod_ssb_hartley_generator(struct fmmod_instance *fmmod, const float* lpr,
			   const float* lmr, int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	const struct fmmod_control *ctl = fmmod->ctl;
	const struct fmmod_flts *flts = &fmmod->flts;
	const struct hilbert_transformer_data *ht = &flts->ht;
	float carrier_freq = 38000.0;
	int i = 0;

	/* Phase shift L-R by 90deg using the Hilbert transformer */
	hilbert_transformer_apply(&flts->ht, lmr, num_samples);

	/* Now shifted L-R signal is in ht->real_buff */

	for(i = 0; i < num_samples; i++) {
		/* Phase lock the ssb oscilator to the master
		 * oscilator */
		cos_osc->current_phase = sin_osc->current_phase;

		/* L-R SSB */
		/* Modulate the shifted signal and the original signal
		 * with two sine waves that also have 90deg phase difference
		 * -to preserve the phase difference also on the carrier-. Then
		 * add them to get the lower sideband (the upper sideband will
		 * be canceled-out) */
		out[i] =  ht->real_buff[i] *
			  osc_get_sample_for_freq(cos_osc, carrier_freq);
		out[i] += lmr[i] *
			  osc_get_sample_for_freq(sin_osc, carrier_freq);

		out[i] *= ctl->stereo_carrier_gain * 1.5;

		/* L + R */
		out[i] += lpr[i];

		/* Stereo Pilot at 19KHz */
		out[i] += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

		/* RDS symbols modulated by the 57KHz carrier (3 x Pilot) */
		out[i] += ctl->rds_gain * osc_get_57Khz_sample(sin_osc) *
					  rds_get_next_sample(&fmmod->rds_enc);

		/* Set mpx gain percentage */
		out[i] *= ctl->mpx_gain;

		osc_increase_phase(sin_osc);
	}

	return 0;
}


/*******************\
* PROCESSING THREAD *
\*******************/

static void*
fmmod_process(struct fmmod_instance *fmmod)
{
	struct resampler_data *rsmpl = &fmmod->rsmpl;
	const struct fmmod_flts *flts = &fmmod->flts;
	struct fmmod_control *ctl = fmmod->ctl;
	mpx_generator get_mpx_samples;
	float *left_in = NULL;
	float *right_in = NULL;
	float *upsampled_audio_l = NULL;
	float *upsampled_audio_r = NULL;
	float *lpr_buf = NULL;
	float *lmr_buf = NULL;
	float lpr = 0.0;
	float lmr = 0.0;
	int frames_generated = 0;
	int i = 0;
	int ret = 0;

	/* Input audio buffers */
	left_in = fmmod->inbuf_l;
	right_in = fmmod->inbuf_r;

	/* Upsampled audio buffers */
	upsampled_audio_l = fmmod->uaudio_buf_0;
	upsampled_audio_r = fmmod->uaudio_buf_1;

	/* L + R / L - R buffers (reuse upsampled buffers) */
	lpr_buf = fmmod->uaudio_buf_0;
	lmr_buf = fmmod->uaudio_buf_1;

	/* Apply a low-pass filter to the audio signal so that
	 * it doesn't hit the 19Khz pilot */
	ret = pthread_mutex_trylock(&fmmod->inbuf_mutex);
	if (ret != 0) {
		if (ret == EBUSY) {
			utils_wrn("[FMMOD] Buffer overrun, skipping this period\n");
			return fmmod;
		}
		goto done;
	}

	if (ctl->use_audio_lpf) {
		lpf_filter_apply(&flts->lpf_l, left_in, left_in,
				 fmmod->num_in_samples, 1.0);
		lpf_filter_apply(&flts->lpf_r, right_in, right_in,
				 fmmod->num_in_samples, 1.0);
	}

	/* Upsample audio to the sample rate of the main oscilator,
	 * apply a low-pass filter in the process */
	pthread_mutex_lock(&fmmod->uaudio_buf_mutex);
	frames_generated = resampler_upsample_audio(rsmpl, left_in, right_in,
						    upsampled_audio_l,
						    upsampled_audio_r,
						    fmmod->num_in_samples,
						    fmmod->upsampled_num_samples);
	pthread_mutex_unlock(&fmmod->inbuf_mutex);
	if (unlikely(frames_generated <= 0)) {
		pthread_mutex_unlock(&fmmod->uaudio_buf_mutex);
		if (frames_generated < 0)
			ret = FMMOD_ERR_RESAMPLER_ERR;
		goto done;
	}

	/* Move L + R to buffer 0 and L - R to buffer 1 */
	for (i = 0; i < frames_generated; i++) {
		lpr = upsampled_audio_l[i] + upsampled_audio_r[i];
		lmr = upsampled_audio_l[i] - upsampled_audio_r[i];
		lpr_buf[i] = lpr;
		lmr_buf[i] = lmr;
	}

	/* Choose modulation method */
	switch (ctl->stereo_modulation) {
	case FMMOD_MONO:
		get_mpx_samples = fmmod_mono_generator;
		break;
	case FMMOD_SSB_HARTLEY:
		get_mpx_samples = fmmod_ssb_hartley_generator;
		break;
	case FMMOD_SSB_LPF:
		get_mpx_samples = fmmod_ssb_lpf_generator;
		break;
	case FMMOD_DSB:
	default:
		get_mpx_samples = fmmod_dsb_generator;
		break;
	}

	/* Create the multiplex signal */
	pthread_mutex_lock(&fmmod->mpx_buf_mutex);
	get_mpx_samples(fmmod, lpr_buf, lmr_buf, frames_generated, fmmod->umpxbuf);
	pthread_mutex_unlock(&fmmod->uaudio_buf_mutex);

	/* Now downsample to the output sample rate */
	frames_generated = resampler_downsample_mpx(rsmpl, fmmod->umpxbuf, fmmod->outbuf,
						    frames_generated,
						    fmmod->num_out_samples);
	if (unlikely(frames_generated <= 0)) {
		pthread_mutex_unlock(&fmmod->mpx_buf_mutex);
		if (frames_generated < 0)
			ret = FMMOD_ERR_RESAMPLER_ERR;
		goto done;
	}

	/* Update mpx output peak gain */
	ctl->peak_mpx_out = 0;
	for (i = 0; i < frames_generated; i++) {
		if (fmmod->outbuf[i] > ctl->peak_mpx_out)
			ctl->peak_mpx_out = fmmod->outbuf[i];
	}

	/* When we start generating frames the resampler needs a few
	 * periods to start generating the expected number of output
	 * samples, so skip those initial periods to avoid sending
	 * fewer samples to the socket/rtp server. */
	if (unlikely(frames_generated != fmmod->num_out_samples)) {
		pthread_mutex_unlock(&fmmod->mpx_buf_mutex);
		goto done;
	}

	/* Write raw MPX signal to socket */
	write_to_sock(fmmod, fmmod->outbuf, frames_generated);

	/* Send out a FLAC-encoded version of the signal as an RTP stream */
	rtp_server_send_buffer(&fmmod->rtpsrv, fmmod->outbuf, frames_generated);

	pthread_mutex_unlock(&fmmod->mpx_buf_mutex);

 done:
	if (ret < 0) {
		utils_err("[FMMMOD] Error while processing: %i\n", ret);
		raise(SIGTERM);
	}
	return fmmod;
}

static void*
fmmod_process_loop(void* arg)
{
	struct fmmod_instance *fmmod = (struct fmmod_instance *)arg;

	while(fmmod->active) {
		pthread_mutex_lock(&fmmod->proc_mutex);
		while (pthread_cond_wait(&fmmod->proc_trigger,
					 &fmmod->proc_mutex) != 0);

		if(!fmmod->active) {
			pthread_mutex_unlock(&fmmod->proc_mutex);
			break;
		}

		fmmod_process(fmmod);

		pthread_mutex_unlock(&fmmod->proc_mutex);
	}

	return arg;
}


/****************\
* JACK CALLBACKS *
\****************/

/**
 * Main process callback -here is where the magic happens-
 */
static int
fmmod_process_cb(jack_nframes_t num_samples, void *arg)
{
	struct fmmod_instance *fmmod = (struct fmmod_instance *)arg;
	struct fmmod_flts *flts = &fmmod->flts;
	struct fmmod_control *ctl = fmmod->ctl;
	const jack_default_audio_sample_t *left_in = NULL;
	const jack_default_audio_sample_t *right_in = NULL;
	float tmp_gain_l = 0.0;
	float tmp_gain_r = 0.0;
	int i = 0;

	/* FMmod is inactive, don't do any processing */
	if (!fmmod->active)
		return 0;

	/* No frames received or underrun, ignore this period */
	if (unlikely(!num_samples || num_samples < fmmod->num_in_samples)) {
		utils_dbg("[FMMOD] got underrun, skipping period\n");
		return 0;
	}

	/* Got more frames than expected */
	if (unlikely(num_samples > fmmod->num_in_samples)) {
		utils_err("[FMMOD] got excessive input samples\n");
		return FMMOD_ERR_INVALID_INPUT;
	}

	/* Try to consume input, if previous buffer is still not in
	 * we got an overrun so skip this period */
	if (pthread_mutex_trylock(&fmmod->inbuf_mutex) != 0) {
		utils_dbg("[FMMOD] got overrun, skipping period\n");
		return 0;
	}

	/* Input */
	left_in = (float *) jack_port_get_buffer(fmmod->inL, num_samples);
	right_in = (float *) jack_port_get_buffer(fmmod->inR, num_samples);

	/* If pre-emphasis is requested, run the input buffers through
	 * the pre-emphasis filter in the time domain, else just copy them
	 * to inbuf_* */
	if (ctl->preemph_tau != LPF_PREEMPH_NONE) {
		for(i = 0; i < num_samples; i++) {
			fmmod->inbuf_l[i] = fmpreemph_filter_apply(&flts->fmprf_l,
							left_in[i],
							ctl->preemph_tau);
			fmmod->inbuf_r[i] = fmpreemph_filter_apply(&flts->fmprf_r,
							right_in[i],
							ctl->preemph_tau);
		}
	} else {
		memcpy(fmmod->inbuf_l, left_in, num_samples * sizeof(float));
		memcpy(fmmod->inbuf_r, right_in, num_samples * sizeof(float));
	}

	/* Update audio gain levels */
	for(i = 0, tmp_gain_l = 0.0, tmp_gain_r = 0.0;
	    i < num_samples; i++) {
		fmmod->inbuf_l[i] *= ctl->audio_gain;
		if(fmmod->inbuf_l[i] > tmp_gain_l)
			tmp_gain_l = fmmod->inbuf_l[i];
		fmmod->inbuf_r[i] *= ctl->audio_gain;
		if(fmmod->inbuf_r[i] > tmp_gain_r)
			tmp_gain_r = fmmod->inbuf_r[i];
	}

	/* We are done with inbuf, let mutex go and
	 * trigger the processing thread to start */
	pthread_mutex_unlock(&fmmod->inbuf_mutex);
	pthread_cond_signal(&fmmod->proc_trigger);

	ctl->peak_audio_in_l = tmp_gain_l;
	ctl->peak_audio_in_r = tmp_gain_r;

	return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
static void
fmmod_shutdown(void *arg)
{
	struct fmmod_instance *fmmod = (struct fmmod_instance *)arg;
	fmmod_destroy(fmmod, 1);
	return;
}


/************************\
* INIT / DESTROY HELPERS *
\************************/

static int
fmmod_connect(struct fmmod_instance *fmmod)
{
	jack_options_t options = JackNoStartServer;
	jack_status_t status;
	int ret = 0;

	/* Open a client connection to the default JACK server */
	fmmod->client = jack_client_open("FMmod", options, &status, NULL);
	if (fmmod->client == NULL) {
		if (status & JackServerFailed)
			utils_err("[FMMOD] Unable to connect to JACK server\n");
		else
			utils_err("[FMMOD] jack_client_open() failed (0x%2.0x)\n",
				  status);
		return FMMOD_ERR_JACKD_ERR;
	}

	if (status & JackNameNotUnique) {
		utils_err("[FMMOD] Another instance of FMmod is still active\n");
		ret = FMMOD_ERR_ALREADY_RUNNING;
		goto cleanup;
	}

	/* Check if JACK is running with real time priority and print
	 * a warning in case it doesn't */
	if(!jack_is_realtime(fmmod->client))
		utils_wrn("[JACKD] Doesn't run with realtime priority\n");

	/* Register callbacks on JACK */
	jack_set_process_callback(fmmod->client, fmmod_process_cb, fmmod);
	jack_on_shutdown(fmmod->client, fmmod_shutdown, fmmod);

	/* Register input ports */
	fmmod->inL = jack_port_register(fmmod->client, "AudioL",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput | JackPortIsTerminal,
					0);
	if (fmmod->inL == NULL) {
		utils_err("[FMMOD] Unable to register AudioL port\n");
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	fmmod->inR = jack_port_register(fmmod->client, "AudioR",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput | JackPortIsTerminal,
					0);
	if (fmmod->inR == NULL) {
		utils_err("[FMMOD] Unable to register AudioR port\n");
		ret = FMMOD_ERR_JACKD_ERR;
	}

 cleanup:
	if (ret < 0) {
		if (fmmod->inL)
			jack_port_unregister(fmmod->client, fmmod->inL);
		jack_client_close(fmmod->client);
	} else
		utils_dbg("[FMMOD] Registered with JACKD\n");

	return ret;
}

static void
fmmod_destroy_locks(struct fmmod_instance *fmmod)
{
	pthread_mutex_destroy(&fmmod->inbuf_mutex);
	pthread_mutex_destroy(&fmmod->uaudio_buf_mutex);
	pthread_mutex_destroy(&fmmod->mpx_buf_mutex);
	pthread_mutex_destroy(&fmmod->proc_mutex);
	pthread_cond_destroy(&fmmod->proc_trigger);
	utils_dbg("[FMMOD] Locks destroyed\n");
}

static void
fmmod_init_locks(struct fmmod_instance *fmmod)
{
	pthread_mutex_init(&fmmod->inbuf_mutex, NULL);
	pthread_mutex_init(&fmmod->uaudio_buf_mutex, NULL);
	pthread_mutex_init(&fmmod->mpx_buf_mutex, NULL);
	pthread_mutex_init(&fmmod->proc_mutex, NULL);
	pthread_cond_init(&fmmod->proc_trigger, NULL);
	utils_dbg("[FMMOD] Locks initialized\n");
}

static void
fmmod_free_buffers(const struct fmmod_instance *fmmod)
{
	if (fmmod->inbuf_l != NULL)
		free(fmmod->inbuf_l);
	if (fmmod->inbuf_r != NULL)
		free(fmmod->inbuf_r);
	if (fmmod->uaudio_buf_0 != NULL)
		free(fmmod->uaudio_buf_0);
	if (fmmod->uaudio_buf_1 != NULL)
		free(fmmod->uaudio_buf_1);
	if (fmmod->umpxbuf != NULL)
		free(fmmod->umpxbuf);
	if (fmmod->outbuf != NULL)
		free(fmmod->outbuf);
	utils_dbg("[FMMOD] Buffers freed\n");
}

static int
fmmod_init_buffers(struct fmmod_instance *fmmod, uint32_t jack_samplerate)
{
	uint32_t inbuf_len = 0;
	uint32_t ssb_lpf_delay_buf_len = 0;
	uint32_t upsampled_buf_len = 0;
	uint32_t output_buf_len = 0;
	int ret = 0;

	/* Allocate input audio buffers */
	inbuf_len = fmmod->num_in_samples *
		    sizeof(jack_default_audio_sample_t);

	fmmod->inbuf_l = (float *) malloc(inbuf_len);
	if (fmmod->inbuf_l == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->inbuf_l, 0, inbuf_len);

	fmmod->inbuf_r = (float *) malloc(inbuf_len);
	if (fmmod->inbuf_r == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->inbuf_r, 0, inbuf_len);

	/* Allocate buffers for the upsampled signals. Use separate
	 * buffers for L/R to make use of SoXr's OpenMP code */
	fmmod->upsampled_num_samples = num_resampled_samples(jack_samplerate,
							OSC_SAMPLE_RATE,
							fmmod->num_in_samples);
	upsampled_buf_len = fmmod->upsampled_num_samples *
			    sizeof(jack_default_audio_sample_t);

	/* Upsampled audio */
	fmmod->uaudio_buf_0 = (float *) malloc(upsampled_buf_len);
	if (fmmod->uaudio_buf_0 == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_0, 0, upsampled_buf_len);

	fmmod->uaudio_buf_1 = (float *) malloc(upsampled_buf_len);
	if (fmmod->uaudio_buf_1 == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_1, 0, upsampled_buf_len);

	/* Upsampled MPX */
	fmmod->umpxbuf = (float *) malloc(upsampled_buf_len);
	if (fmmod->umpxbuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->umpxbuf, 0, upsampled_buf_len);


	/* Allocate output buffer */
	fmmod->num_out_samples = num_resampled_samples(OSC_SAMPLE_RATE,
						FMMOD_OUTPUT_SAMPLERATE,
						fmmod->upsampled_num_samples);
	output_buf_len = fmmod->num_out_samples * sizeof(float);

	fmmod->outbuf = (float *) malloc(output_buf_len);
	if (fmmod->outbuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->outbuf, 0, output_buf_len);

	utils_dbg("[FMMOD] Buffers initialized\n");
	return  0;

 cleanup:
	utils_err("[FMMOD] Could not initialize buffers !\n");
	fmmod_free_buffers(fmmod);
	return ret;
}

static int
fmmod_init_osc(struct fmmod_instance *fmmod)
{
	int ret = 0;

	/* Initialize the main oscilator */
	ret = osc_initialize(&fmmod->sin_osc, OSC_SAMPLE_RATE, OSC_TYPE_SINE);
	if (ret < 0) {
		utils_err("[OSC] Init for sine osc failed with code: %i\n", ret);
		return FMMOD_ERR_OSC_ERR;
	}

	/* Initialize the cosine oscilator of the Hartley modulator */
	ret = osc_initialize(&fmmod->cos_osc, OSC_SAMPLE_RATE, OSC_TYPE_COSINE);
	if (ret < 0) {
		utils_err("[OSC] Init for cosine osc failed with code: %i\n", ret);
		return FMMOD_ERR_OSC_ERR;
	}

	utils_dbg("[OSC] Init complete\n");

	return 0;
}

static void
fmmod_destroy_filters(struct fmmod_instance *fmmod)
{
	struct fmmod_flts *flts = &fmmod->flts;

	lpf_filter_destroy(&flts->lpf_l);

	lpf_filter_destroy(&flts->lpf_r);

	lpf_filter_destroy(&flts->ssb_lpf);

	hilbert_transformer_destroy(&flts->ht);

	utils_dbg("[FILTERS] Destroyed\n");
}

static int
fmmod_init_filters(struct fmmod_instance *fmmod, uint32_t jack_samplerate)
{
	struct fmmod_flts *flts = &fmmod->flts;
	int ret = 0;

	/* Initialize audio FM pre-emphasis IIR filter */
	ret = fmpreemph_filter_init(&flts->fmprf_l, (float) jack_samplerate);
	if(ret < 0) {
		utils_err("[FILTERS] Pre-emphasis filter (L) init failed with code: %i\n", ret);
		ret = FMMOD_ERR_AFLT;
		return ret;
	}

	ret = fmpreemph_filter_init(&flts->fmprf_r, (float) jack_samplerate);
	if(ret < 0) {
		utils_err("[FILTERS] Pre-emphasis filter (R) init failed with code: %i\n", ret);
		ret = FMMOD_ERR_AFLT;
		return ret;
	}

	/* Initialize audio low-pass FFT filter for protecting the pilot */
	ret = lpf_filter_init(&flts->lpf_l, AFLT_CUTOFF_FREQ, jack_samplerate,
			      fmmod->num_in_samples, AFLT_LPF_OVERLAP_FACTOR);
	if (ret < 0) {
		utils_err("[FILTERS] LPF (L) init failed with code: %i\n", ret);
		ret = FMMOD_ERR_AFLT;
		goto cleanup;
	}

	ret = lpf_filter_init(&flts->lpf_r, AFLT_CUTOFF_FREQ, jack_samplerate,
			      fmmod->num_in_samples, AFLT_LPF_OVERLAP_FACTOR);
	if (ret < 0) {
		utils_err("[FILTERS] LPF (R) init failed with code: %i\n", ret);
		ret = FMMOD_ERR_AFLT;
		goto cleanup;
	}

	/* Initialize the low pass FFT filter for the filter-based SSB modulator */
	ret = lpf_filter_init(&flts->ssb_lpf, 38000, OSC_SAMPLE_RATE,
			      fmmod->upsampled_num_samples, SSB_LPF_OVERLAP_FACTOR);
	if (ret < 0) {
		utils_err("[FILTERS] LPF (SSB) init failed with code: %i\n", ret);
		ret = FMMOD_ERR_LPF;
		goto cleanup;
	}


	/* Initialize the Hilbert transformer for the Hartley modulator */
	ret = hilbert_transformer_init(&flts->ht, fmmod->upsampled_num_samples);
	if (ret < 0) {
		utils_err("[FILTERS] Hilbert transformer init failed with code: %i\n", ret);
		ret = FMMOD_ERR_HILBERT;
		goto cleanup;
	}

 cleanup:
	if (ret < 0) {
		switch (ret) {
		case FMMOD_ERR_HILBERT:
			lpf_filter_destroy(&flts->ssb_lpf);
			/* Fallthrough */
		case FMMOD_ERR_LPF:
			lpf_filter_destroy(&flts->lpf_l);
			lpf_filter_destroy(&flts->lpf_r);
			/* Fallthrough */
		default:
			break;
		}
		utils_err("[FILTERS] Init failed with code: %i\n", ret);
	} else
		utils_dbg("[FILTERS] Init complete\n");

	return ret;
}

static int
fmmod_init_outsock(void)
{
	uint32_t uid = 0;
	char sock_path[32] = { 0 };
	int ret = 0;

	/* Create a named pipe (fifo socket) for sending
	 * out the raw mpx signal (float32) */
	uid = getuid();
	snprintf(sock_path, 32, "/run/user/%u/jmpxrds.sock", uid);
	ret = mkfifo(sock_path, 0600);
	if ((ret < 0) && (errno != EEXIST)) {
		utils_perr("[OUTSOCK] Unable to create socket, mkfifo()");
		return FMMOD_ERR_SOCK_ERR;
	}
	utils_dbg("[OUTSOCK] Init complete\n");

	return 0;
}

static void
fmmod_outsock_destroy(const struct fmmod_instance *fmmod)
{
	uint32_t uid = 0;
	char sock_path[32] = { 0 };

	close(fmmod->out_sock_fd);
	uid = getuid();
	snprintf(sock_path, 32, "/run/user/%u/jmpxrds.sock", uid);
	unlink(sock_path);
	utils_dbg("[OUTSOCK] Destroyed\n");
}

static int
fmmod_init_ctl(struct fmmod_instance *fmmod)
{
	struct fmmod_control *ctl = NULL;

	/* Initialize the control I/O channel */
	fmmod->ctl_map = utils_shm_init(FMMOD_CTL_SHM_NAME,
					sizeof(struct fmmod_control));
	if (!fmmod->ctl_map) {
		utils_err("[FMMOD] Unable to create control channel\n");
		return FMMOD_ERR_SHM_ERR;
	}
	fmmod->ctl = (struct fmmod_control*) fmmod->ctl_map->mem;

	ctl = fmmod->ctl;
	ctl->audio_gain = 0.45;
	ctl->pilot_gain = 0.04;
	ctl->rds_gain = 0.03;
	ctl->stereo_carrier_gain = 0.8;
	ctl->mpx_gain = 0.75;
	ctl->stereo_modulation = FMMOD_DSB;
	ctl->use_audio_lpf = 1;
	ctl->preemph_tau = LPF_PREEMPH_50US;
	ctl->sample_rate = FMMOD_OUTPUT_SAMPLERATE;
	ctl->max_samples = fmmod->num_out_samples;

	utils_dbg("[FMMOD] Control channel ready\n");

	return 0;
}


/****************\
* INIT / DESTROY *
\****************/

int
fmmod_initialize(struct fmmod_instance *fmmod)
{
	uint32_t jack_samplerate = 0;
	uint32_t output_buf_len = 0;
	int ret = 0;

	memset(fmmod, 0, sizeof(struct fmmod_instance));

	/* Connect to jack and register as a client */
	ret = fmmod_connect(fmmod);
	if (ret < 0)
		goto cleanup;

	/* Get JACK's sample rate and number of frames JACK will send to process()
	 * (period len), and calculate buffer lengths */
	jack_samplerate = jack_get_sample_rate(fmmod->client);
	fmmod->num_in_samples = jack_get_buffer_size(fmmod->client);
	if (jack_samplerate <= 0 || fmmod->num_in_samples <= 0) {
		utils_err("[FMMOD] Got invalid data from jackd: %i, %i\n",
			  jack_samplerate, fmmod->num_in_samples);
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	/* Initialize buffers */
	ret = fmmod_init_buffers(fmmod, jack_samplerate);
	if (ret < 0)
		goto cleanup;

	/* Initialize locks / triggers */
	fmmod_init_locks(fmmod);

	/* Initialize oscilators */
	ret = fmmod_init_osc(fmmod);
	if (ret < 0)
		goto cleanup;

	/* Initialize resampler */
	ret = resampler_init(&fmmod->rsmpl, jack_samplerate,
			     fmmod->client,
			     OSC_SAMPLE_RATE,
			     RDS_SAMPLE_RATE,
			     FMMOD_OUTPUT_SAMPLERATE);
	if (ret < 0) {
		ret = FMMOD_ERR_RESAMPLER_ERR;
		goto cleanup;
	}

	/* Initialize filters */
	ret = fmmod_init_filters(fmmod, jack_samplerate);
	if (ret < 0)
		goto cleanup;

	/* Initialize RDS encoder */
	ret = rds_encoder_init(&fmmod->rds_enc, fmmod->client, &fmmod->rsmpl);
	if (ret < 0) {
		ret = FMMOD_ERR_RDS_ERR;
		goto cleanup;
	}

	/* Initialize output socket */
	ret = fmmod_init_outsock();
	if (ret < 0)
		goto cleanup;

	/* Initialize the RTP server for sending the FLAC-compressed
	 * mpx signal to a remote host if needed */
	fmmod->rtpsrv.fmmod_client = fmmod->client;
	output_buf_len = fmmod->num_out_samples * sizeof(float);
	ret = rtp_server_init(&fmmod->rtpsrv, output_buf_len,
			      FMMOD_OUTPUT_SAMPLERATE, 5000);
	if (ret < 0) {
		ret = FMMOD_ERR_RTP_ERR;
		goto cleanup;
	}

	/* Initialize control channel */
	ret = fmmod_init_ctl(fmmod);
	if (ret < 0)
		goto cleanup;

	fmmod->active = 1;

	/* Init processing thread */
	ret = jack_client_create_thread(fmmod->client, &fmmod->proc_tid,
					jack_client_real_time_priority(fmmod->client),
					jack_is_realtime(fmmod->client),
					fmmod_process_loop, (void *) fmmod);
	if(ret < 0) {
		utils_err("[JACKD] Could not create processing thread\n");
		return ret;
	}

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */
	ret = jack_activate(fmmod->client);
	if (ret != 0) {
		utils_err("[FMMOD] Could not activate FMmod\n");
		ret = FMMOD_ERR_JACKD_ERR;
	}

 cleanup:
	if (ret < 0) {
		utils_err("[FMMOD] Init failed with code: %i\n", ret);
		fmmod_destroy(fmmod, 0);
		return ret;
	}

	utils_dbg("[FMMOD] Init complete\n");
	return 0;
}

void
fmmod_destroy(struct fmmod_instance *fmmod, int shutdown)
{
	const struct fmmod_flts *flts = &fmmod->flts;

	if (!shutdown) {
		utils_dbg("[FMMOD] Graceful exit\n");
		jack_deactivate(fmmod->client);
		if (fmmod->inL)
			jack_port_unregister(fmmod->client, fmmod->inL);
		if (fmmod->inR)
			jack_port_unregister(fmmod->client, fmmod->inR);
		jack_client_close(fmmod->client);
	} else
		utils_dbg("[FMMOD] Jack dropped fmmod\n");

	fmmod->active = 0;

	utils_dbg("[FMMOD] Deactivated\n");

	/* Let the process thread run for one last time */
	pthread_mutex_lock(&fmmod->proc_mutex);
	pthread_cond_signal(&fmmod->proc_trigger);
	pthread_mutex_unlock(&fmmod->proc_mutex);

	utils_shm_destroy(fmmod->ctl_map, 1);

	utils_dbg("[FMMOD] Control channel closed\n");

	rds_encoder_destroy(&fmmod->rds_enc);

	rtp_server_destroy(&fmmod->rtpsrv);

	resampler_destroy(&fmmod->rsmpl);

	fmmod_destroy_filters(fmmod);

	fmmod_destroy_locks(fmmod);

	fmmod_free_buffers(fmmod);

	fmmod_outsock_destroy(fmmod);

	utils_dbg("[FMMOD] Destroyed\n");

	return;
}
