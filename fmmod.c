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

static int
num_resampled_samples(int in_srate, int out_srate, int num_samples)
{
	float ratio = (float) out_srate / (float) in_srate;
	float olenf = ratio * ((float) num_samples);
	/* Also cover the case where out_srate < in_srate */
	olenf = fmax(olenf, num_samples - 1.0);
	return (int) olenf;
}


static int
write_to_sock(struct fmmod_instance *fmmod, float *samples, int num_samples)
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

static float
get_delayed_lpr_sample(struct fmmod_instance *fmmod, float* lpr, int num_samples, int delay, int idx)
{
	float *previous = fmmod->delay_buf;
	static int previous_num_samples = 0;

	if (num_samples == 0)
		return 0.0;

	/* Called for the first time, initialize tmp buffer */
	if (fmmod->delay_buf == NULL) {
		fmmod->delay_buf = (float*) malloc(fmmod->upsampled_buf_len);
		memset(fmmod->delay_buf, 0, fmmod->upsampled_buf_len);
		previous = fmmod->delay_buf;
		previous_num_samples = delay;
	/* Done with current period, save it to tmp buffer */
	} else if (idx == (num_samples - 1)) {
		memcpy(fmmod->delay_buf, lpr, num_samples * sizeof(float));
		previous = fmmod->delay_buf;
		previous_num_samples = num_samples;
	}

	/* Grab delayed sample from previous period */
	if (idx < delay) {
		idx += (previous_num_samples - delay);
		return previous[idx];
	/* Grab delayed sample from current period */
	} else
		return lpr[idx - delay];
}

/************************\
* FM MPX STEREO ENCODING *
\************************/

/*
 * Mono generator, just L+R plus RDS if
 * available
 */
static int
fmmod_mono_generator(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		     int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct fmmod_control *ctl = fmmod->ctl;
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
fmmod_dsb_generator(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		    int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct fmmod_control *ctl = fmmod->ctl;
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
fmmod_ssb_lpf_generator(struct fmmod_instance *fmmod, float* lpr, float* lmr,
			int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	double saved_phase = 0.0L;
	int i = 0;

	/* Start with the L-R part, save the oscilator's phase
	 * so that we can re-set it when calculating the pilot
	 * and the RDS parts */
	saved_phase = sin_osc->current_phase;
	for(i = 0; i < num_samples; i++) {
		/* AM modulated L - R */
		out[i] = lmr[i] * osc_get_38Khz_sample(sin_osc);
		osc_increase_phase(sin_osc);
	}

	/* Apply the lpf filter to suppres the USB */
	lpf_filter_apply(&fmmod->ssb_lpf, out, out,
		    num_samples, ctl->stereo_carrier_gain);

	/* Now restore the oscilator's phase and add the rest */
	sin_osc->current_phase = saved_phase;
	for(i = 0; i < num_samples; i++) {
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

/************************\
* WEAVER MODULATOR (SSB) *
\************************/

/*
 * For more infos on the Weaver SSB modulator visit
 * http://dp.nonoo.hu/projects/ham-dsp-tutorial/11-ssb-weaver/
 */
static int
fmmod_ssb_weaver_generator(struct fmmod_instance *fmmod, float* lpr, float* lmr,
			   int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	float in_phase = 0.0;
	float quadrature = 0.0;
	float tmp = 0.0;
	int frequency_shift = 0;
	int i = 0;

	for(i = 0; i < num_samples; i++) {
		/* Delayed L + R */
		out[i] = get_delayed_lpr_sample(fmmod, lpr, num_samples,
					     WEAVER_FILTER_TAPS, i);

		/* Stereo pilot at 19KHz */
		out[i] += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

		/* Phase-lock the SSB oscilator to the master
		 * oscilator */
		cos_osc->current_phase = sin_osc->current_phase;

		/* Create a quadrature version of the input signal */
		in_phase = lmr[i] * osc_get_sample_for_freq(sin_osc,
					   sin_osc->sample_rate / 4);

		quadrature = lmr[i] * osc_get_sample_for_freq(cos_osc,
					     cos_osc->sample_rate / 4);

		/* Apply the low pass filter */
		in_phase = iir_ssb_filter_apply(&fmmod->weaver_lpf, in_phase, 0);
		quadrature = iir_ssb_filter_apply(&fmmod->weaver_lpf, quadrature, 1);

		/* Shift it to the carrier frequency and combine in_phase
		 * and quadrature to create the LSB signal we want */
		frequency_shift = (sin_osc->sample_rate / 4) - 38000;
		in_phase *= osc_get_sample_for_freq(sin_osc, frequency_shift);
		frequency_shift = (cos_osc->sample_rate / 4) - 38000;
		quadrature *= osc_get_sample_for_freq(cos_osc, frequency_shift);

		tmp = in_phase + quadrature;
		out[i] += tmp * ctl->stereo_carrier_gain;

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
fmmod_ssb_hartley_generator(struct fmmod_instance *fmmod, float* lpr, float* lmr,
			   int num_samples, float* out)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	struct hilbert_transformer_data *ht = &fmmod->ht;
	int carrier_freq = 38000;
	int i = 0;

	/* Phase shift L-R by 90deg using the Hilbert transformer */
	hilbert_transformer_apply(&fmmod->ht, lmr, num_samples);

	/* Now shifted L+R signal is in ht->real_buff */

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
		out[i] =  (float) (ht->real_buff[i] *
			  osc_get_sample_for_freq(cos_osc, carrier_freq));
		out[i] += lmr[i] *
			  osc_get_sample_for_freq(sin_osc, carrier_freq);

		out[i] *= ctl->stereo_carrier_gain;

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

/****************\
* JACK CALLBACKS *
\****************/

/**
 * Main process callback -here is where the magic happens-
 */
static int
fmmod_process(jack_nframes_t nframes, void *arg)
{
	int i = 0;
	jack_default_audio_sample_t *left_in, *right_in;
	jack_default_audio_sample_t *mpx_out;
	size_t mpx_out_len = 0;
	float *mpxbuf = NULL;
	float *upsampled_audio_l = NULL;
	float *upsampled_audio_r = NULL;
	float *lpr_buf = NULL;
	float *lmr_buf = NULL;
	float lpr = 0.0;
	float lmr = 0.0;
	int frames_generated = 0;
	struct fmmod_instance *fmmod = (struct fmmod_instance *)arg;
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct resampler_data *rsmpl = &fmmod->rsmpl;
	struct audio_filter *aflt = &fmmod->aflt;
	struct fmmod_control *ctl = fmmod->ctl;
	mpx_generator get_mpx_samples;

	/* FMmod is inactive, don't do any processing */
	if (!fmmod->active)
		return 0;

	/* Temporary buffer */
	mpxbuf = fmmod->mpxbuf;

	/* Upsampled audio buffers */
	upsampled_audio_l = fmmod->uaudio_buf_0;
	upsampled_audio_r = fmmod->uaudio_buf_1;

	/* L + R / L - R buffers */
	lpr_buf = fmmod->uaudio_buf_0;
	lmr_buf = fmmod->uaudio_buf_1;

	/* Input */
	left_in = (float *)jack_port_get_buffer(fmmod->inL, nframes);
	right_in = (float *)jack_port_get_buffer(fmmod->inR, nframes);

	/* Output */
	if (fmmod->output_type == FMMOD_OUT_JACK) {
		mpx_out = (float *)jack_port_get_buffer(fmmod->outMPX, nframes);
		mpx_out_len = nframes;
	} else {
		mpx_out = fmmod->sock_outbuf;
		mpx_out_len = fmmod->sock_outbuf_len;
	}
	ctl->peak_audio_in_l = 0;
	ctl->peak_audio_in_r = 0;
	ctl->peak_mpx_out = 0;

	/* No frames received */
	if (!nframes)
		return 0;

	/* Apply audio filter on input and merge the two
	 * channels to prepare the buffer for upsampling */
	audio_filter_apply(aflt, left_in, fmmod->inbuf_l,
			   right_in, fmmod->inbuf_r, nframes,
			   ctl->audio_gain, ctl->use_audio_lpf);

	/* Update audio peak levels */
	for(i = 0; i < nframes; i++) {
		if (fmmod->inbuf_l[i] > ctl->peak_audio_in_l)
			ctl->peak_audio_in_l = fmmod->inbuf_l[i];

		if (fmmod->inbuf_r[i] > ctl->peak_audio_in_r)
			ctl->peak_audio_in_r = fmmod->inbuf_r[i];
	}

	/* Upsample to the sample rate of the main oscilator */
	frames_generated = resampler_upsample_audio(rsmpl, fmmod->inbuf_l,
						    fmmod->inbuf_r,
						    upsampled_audio_l,
						    upsampled_audio_r,
						    nframes,
						    fmmod->upsampled_buf_len);
	if (frames_generated < 0)
		return FMMOD_ERR_RESAMPLER_ERR;

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
	case FMMOD_SSB_WEAVER:
		get_mpx_samples = fmmod_ssb_weaver_generator;
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
	get_mpx_samples(fmmod, lpr_buf, lmr_buf, frames_generated, mpxbuf);

	/* Now downsample to the output sample rate */
	frames_generated = resampler_downsample_mpx(rsmpl, mpxbuf, mpx_out,
						    frames_generated,
						    mpx_out_len);
	if (frames_generated < 0)
		return FMMOD_ERR_RESAMPLER_ERR;

	/* Update mpx output peak gain */
	for (i = 0; i < frames_generated; i++) {
		if (mpx_out[i] > ctl->peak_mpx_out)
			ctl->peak_mpx_out = mpx_out[i];
	}

	/* Write raw MPX signal to socket */
	write_to_sock(fmmod, mpx_out, frames_generated);

	/* Send out a FLAC-encoded version of the signal as an RTP stream */
	rtp_server_send_buffer(&fmmod->rtpsrv, mpx_out, frames_generated);

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

/****************\
* INIT / DESTROY *
\****************/

int
fmmod_initialize(struct fmmod_instance *fmmod, int region)
{
	int ret = 0;
	struct fmmod_control *ctl = NULL;
	uint32_t jack_samplerate = 0;
	uint32_t output_samplerate = 0;
	uint32_t max_process_frames = 0;
	uint8_t preemph_usecs = 0;
	uint32_t uid = 0;
	char sock_path[32] = { 0 };	/* /run/user/<userid>/jmpxrds.sock */
	char *client_name = NULL;
	jack_options_t options = JackNoStartServer;
	jack_status_t status;
	uint32_t osc_samplerate = OSC_SAMPLE_RATE;

	memset(fmmod, 0, sizeof(struct fmmod_instance));

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

	/* Get JACK's sample rate and set output_samplerate */
	jack_samplerate = jack_get_sample_rate(fmmod->client);
	if (jack_samplerate >= FMMOD_OUTPUT_SAMPLERATE_MIN) {
		output_samplerate = jack_samplerate;
		fmmod->output_type = FMMOD_OUT_JACK;
	} else if (jack_samplerate > 0) {
		output_samplerate = FMMOD_OUTPUT_SAMPLERATE_MIN;
		fmmod->output_type = FMMOD_OUT_NOJACK;
	} else {
		utils_err("[FMMOD] Got invalid samplerate from jackd: %i\n",
			  jack_samplerate);
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	/* Get maximum number of frames JACK will send to process() and
	 * calculate buffer lengths */
	max_process_frames = jack_get_buffer_size(fmmod->client);
	fmmod->inbuf_len = max_process_frames *
	    		   sizeof(jack_default_audio_sample_t);
	fmmod->upsampled_num_samples = num_resampled_samples(jack_samplerate,
							osc_samplerate,
							max_process_frames);
	fmmod->upsampled_buf_len = fmmod->upsampled_num_samples *
				   sizeof(jack_default_audio_sample_t);

	/* Allocate input audio buffers */
	fmmod->inbuf_l = (float *)malloc(fmmod->inbuf_len);
	if (fmmod->inbuf_l == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->inbuf_l, 0, fmmod->inbuf_len);

	fmmod->inbuf_r = (float *)malloc(fmmod->inbuf_len);
	if (fmmod->inbuf_r == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->inbuf_r, 0, fmmod->inbuf_len);

	/* Allocate buffers for the upsampled audio. Use separate
	 * buffers for L/R to make use of SoXr's OpenMP code */
	fmmod->uaudio_buf_0 = (float *)malloc(fmmod->upsampled_buf_len);
	if (fmmod->uaudio_buf_0 == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_0, 0, fmmod->upsampled_buf_len);

	fmmod->uaudio_buf_1 = (float *)malloc(fmmod->upsampled_buf_len);
	if (fmmod->uaudio_buf_1 == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_1, 0, fmmod->upsampled_buf_len);

	/* Allocate output buffer for MPX */
	fmmod->mpxbuf = (float *)malloc(fmmod->upsampled_buf_len);
	if (fmmod->mpxbuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->mpxbuf, 0, fmmod->upsampled_buf_len);

	/* Initialize the main oscilator */
	ret = osc_initialize(&fmmod->sin_osc, osc_samplerate, OSC_TYPE_SINE);
	if (ret < 0) {
		utils_err("[OSC] Init for sine osc failed with code: %i\n", ret);
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the cosine oscilator of the weaver modulator */
	ret = osc_initialize(&fmmod->cos_osc, osc_samplerate, OSC_TYPE_COSINE);
	if (ret < 0) {
		utils_err("[OSC] Init for cosine osc failed with code: %i\n", ret);
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the low pass FFT filter for the filter-based SSB modulator */
	ret = lpf_filter_init(&fmmod->ssb_lpf, 38000, osc_samplerate,
			      fmmod->upsampled_num_samples);
	if (ret < 0) {
		utils_err("[LPF] Init failed with code: %i\n", ret);
		ret = FMMOD_ERR_LPF;
		goto cleanup;
	}
	/* Initialize the low pass filters of the Weaver modulator */
	iir_ssb_filter_init(&fmmod->weaver_lpf);
	/* Initialize the Hilbert transformer for the Hartley modulator */
	ret = hilbert_transformer_init(&fmmod->ht, fmmod->upsampled_num_samples);
	if (ret < 0) {
		utils_err("[HILBERT] Init failed with code: %i\n",
			  ret);
		ret = FMMOD_ERR_HILBERT;
		goto cleanup;
	}

	/* Initialize resampler */
	ret = resampler_init(&fmmod->rsmpl, jack_samplerate,
			     osc_samplerate,
			     RDS_SAMPLE_RATE,
			     output_samplerate, max_process_frames);
	if (ret < 0) {
		utils_err("[RESAMPLER] Init failed with code: %i\n", ret);
		ret = FMMOD_ERR_RESAMPLER_ERR;
		goto cleanup;
	}

	/* Initialize audio filter */
	switch (region) {
	case FMMOD_REGION_US:
		preemph_usecs = 75;
		break;
	case FMMOD_REGION_EU:
	case FMMOD_REGION_WORLD:
		preemph_usecs = 50;
		break;
	default:
		preemph_usecs = 50;
		break;
	}

	/* The cutoff frequency is set so that the filter's
	 * maximum drop is at 19KHz (the pilot tone) */
	ret = audio_filter_init(&fmmod->aflt, 16500, jack_samplerate,
				max_process_frames, preemph_usecs);
	if (ret < 0) {
		utils_err("[AFLT] Init failed with code: %i\n", ret);
		ret = FMMOD_ERR_AFLT;
		goto cleanup;
	}

	/* Initialize RDS encoder */
	ret = rds_encoder_init(&fmmod->rds_enc, &fmmod->rsmpl);
	if (ret < 0) {
		utils_err("[RDS] Init failed with code: %i\n", ret);
		ret = FMMOD_ERR_RDS_ERR;
		goto cleanup;
	}
	fmmod->rds_enc.fmmod_client = fmmod->client;

	/* Register callbacks on JACK */
	jack_set_process_callback(fmmod->client, fmmod_process, fmmod);
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
		goto cleanup;
	}

	/* Create a named pipe (fifo socket) for sending
	 * out the raw mpx signal (float32) */
	uid = getuid();
	snprintf(sock_path, 32, "/run/user/%u/jmpxrds.sock", uid);
	ret = mkfifo(sock_path, 0600);
	if ((ret < 0) && (errno != EEXIST)) {
		ret = FMMOD_ERR_SOCK_ERR;
		utils_perr("[FMMOD] Unable to create socket, mkfifo()");
		goto cleanup;
	}

	/* Register output port if possible, if not allocate
	 * our own buffers since we can't use JACK's */
	if (fmmod->output_type == FMMOD_OUT_JACK) {
		fmmod->outMPX = jack_port_register(fmmod->client, "MPX",
						   JACK_DEFAULT_AUDIO_TYPE,
						   JackPortIsOutput |
						   JackPortIsTerminal, 0);
		if (fmmod->outMPX == NULL) {
			utils_err("[FMMOD] Unable to register MPX port\n");
			ret = FMMOD_ERR_JACKD_ERR;
			goto cleanup;
		}
	} else {
		fmmod->sock_outbuf_len = num_resampled_samples(osc_samplerate,
							       output_samplerate,
							       fmmod->upsampled_num_samples);
		fmmod->sock_outbuf_len *= sizeof(float);
		fmmod->sock_outbuf = (float *)malloc(fmmod->sock_outbuf_len);
		if (fmmod->sock_outbuf == NULL) {
			ret = FMMOD_ERR_NOMEM;
			goto cleanup;
		}
		memset(fmmod->sock_outbuf, 0, fmmod->sock_outbuf_len);
	}

	/* Initialize the RTP server for sending the FLAC-compressed
	 * mpx signal to a remote host if needed */
	fmmod->rtpsrv.fmmod_client = fmmod->client;
	ret = rtp_server_init(&fmmod->rtpsrv, output_samplerate,
			      max_process_frames, 5000);
	if (ret < 0) {
		utils_err("[RTP] Init failed with code: %i\n", ret);
		ret = FMMOD_ERR_RTP_ERR;
		goto cleanup;
	}

	/* Initialize the control I/O channel */
	fmmod->ctl_map = utils_shm_init(FMMOD_CTL_SHM_NAME,
					sizeof(struct fmmod_control));
	if (!fmmod->ctl_map) {
		utils_err("[FMMOD] Unable to create control channel\n");
		ret = FMMOD_ERR_SHM_ERR;
		goto cleanup;
	}
	fmmod->ctl = (struct fmmod_control*) fmmod->ctl_map->mem;

	ctl = fmmod->ctl;
	ctl->audio_gain = 0.45;
	ctl->pilot_gain = 0.083;
	ctl->rds_gain = 0.026;
	ctl->stereo_carrier_gain = 1.0;
	ctl->mpx_gain = 1.0;
	ctl->stereo_modulation = FMMOD_DSB;
	ctl->use_audio_lpf = 1;

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */
	ret = jack_activate(fmmod->client);
	if (ret != 0) {
		utils_err("[FMMOD] Could not activate FMmod\n");
		ret = FMMOD_ERR_JACKD_ERR;
	} else
		fmmod->active = 1;

 cleanup:
	if (ret < 0) {
		fmmod_destroy(fmmod, 0);
		return ret;
	} else
		return 0;
}

void
fmmod_destroy(struct fmmod_instance *fmmod, int shutdown)
{
	int uid = 0;
	char sock_path[32] = { 0 };

	if (!shutdown)
		jack_deactivate(fmmod->client);

	fmmod->active = 0;

	utils_dbg("[FMMOD] deactivated\n");

	utils_shm_destroy(fmmod->ctl_map, 1);

	rds_encoder_destroy(&fmmod->rds_enc);

	utils_dbg("[RDS] destroyed\n");

	rtp_server_destroy(&fmmod->rtpsrv);

	utils_dbg("[RTP] destroyed\n");

	resampler_destroy(&fmmod->rsmpl);

	utils_dbg("[RESAMPLER] destroyed\n");

	audio_filter_destroy(&fmmod->aflt);

	utils_dbg("[AFLT] destroyed\n");

	lpf_filter_destroy(&fmmod->ssb_lpf);

	utils_dbg("[LPF] destroyed\n");

	hilbert_transformer_destroy(&fmmod->ht);

	utils_dbg("[HILBERT] destroyed\n");

	if (fmmod->inbuf_l != NULL)
		free(fmmod->inbuf_l);
	if (fmmod->inbuf_r != NULL)
		free(fmmod->inbuf_r);
	if (fmmod->uaudio_buf_0 != NULL)
		free(fmmod->uaudio_buf_0);
	if (fmmod->uaudio_buf_1 != NULL)
		free(fmmod->uaudio_buf_1);
	if (fmmod->delay_buf != NULL)
		free(fmmod->delay_buf);
	if (fmmod->mpxbuf != NULL)
		free(fmmod->mpxbuf);

	close(fmmod->out_sock_fd);
	uid = getuid();
	snprintf(sock_path, 32, "/run/user/%u/jmpxrds.sock", uid);
	unlink(sock_path);

	if (fmmod->sock_outbuf != NULL)
		free(fmmod->sock_outbuf);

	utils_dbg("[FMMOD] destroyed\n");

	return;
}
