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
#include <stdio.h>		/* For printf */
#include <sys/mman.h>		/* For shm_open */
#include <sys/stat.h>		/* For mode constants */
#include <fcntl.h>		/* For O_* and F_* constants */
#include <errno.h>		/* For errno and EEXIST */

/*********\
* HELPERS *
\*********/

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
		perror("write()");
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
		fmmod->delay_buf = malloc(fmmod->uaudio_buf_len);
		memset(fmmod->delay_buf, 0, fmmod->uaudio_buf_len);
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

static float
get_mono_sample(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		int num_samples, int idx)
{
	/* No stereo pilot / subcarrier */
	return lpr[idx];
}


/*
 * Standard Double Sideband with Suppressed Carrier (DSBSC)
 * The input sample is AM modulated with a sine wave
 * at 38KHz (twice the pilot's frequency)
 */
static float
get_dsb_sample(struct fmmod_instance *fmmod, float* lpr, float* lmr,
	       int num_samples, int idx)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	float out = 0.0;

	/* L + R */
	out = lpr[idx];

	/* Stereo Pilot at 19KHz */
	out += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

	/* AM modulated L - R */
	out += lmr[idx] * osc_get_38Khz_sample(sin_osc) *
		          ctl->stereo_carrier_gain;
	return out;
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

/********************************\
* FIR FILTER BASED SSB MODULATOR *
\********************************/

/*
 * A simple FIR low pass filter that cuts off anything above
 * the carrier (the upper side band). This is a VSB
 * modulator since the FIR filter is not that steep.
 */
static float
get_ssb_fir_sample(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		   int num_samples, int idx)
{
	int i = 0;
	float out = 0.0;
	float tmp = 0.0;
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct fmmod_control *ctl = fmmod->ctl;

	/* Delayed L + R */
	out = get_delayed_lpr_sample(fmmod, lpr, num_samples,
				     FIR_FILTER_HALF_SIZE, idx);

	/* Stereo pilot at 19KHz */
	out += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

	/* AM Modulated L - R with suppresed USB */
	tmp = lmr[idx] * osc_get_38Khz_sample(sin_osc);
	tmp = fir_filter_apply(&fmmod->ssb_fir_lpf, tmp, 0);
	fir_filter_update(&fmmod->ssb_fir_lpf);
	out += tmp * ctl->stereo_carrier_gain;

	return out;
}

/************************\
* WEAVER MODULATOR (SSB) *
\************************/

/*
 * For more infos on the Weaver SSB modulator visit
 * http://dp.nonoo.hu/projects/ham-dsp-tutorial/11-ssb-weaver/
 *
 * Since the IIR filter is not good enough this is almost a
 * VSB modulator plus the IIR filter messes up with the phases.
 * It's a bit heavier than the Hartley modulator but performs
 * better and has a steeper spectrum than the filter-based one.
 */
static float
get_ssb_weaver_sample(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		      int num_samples, int idx)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	float in_phase = 0.0;
	float quadrature = 0.0;
	float out = 0.0;
	float tmp = 0.0;
	int frequency_shift = 0;
	int i = 0;

	/* Delayed L + R */
	out = get_delayed_lpr_sample(fmmod, lpr, num_samples,
				     WEAVER_FILTER_TAPS, idx);

	/* Stereo pilot at 19KHz */
	out += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

	/* Phase lock the ssb oscilator to the master
	 * oscilator */
	cos_osc->current_phase = sin_osc->current_phase;

	/* Create a quadrature version of the input signal */
	in_phase = lmr[idx] * osc_get_sample_for_freq(sin_osc,
						    sin_osc->sample_rate / 4);
	quadrature = lmr[idx] * osc_get_sample_for_freq(cos_osc,
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

	/* Set the output gain percentage */
	out += tmp * ctl->stereo_carrier_gain;

	return out;
}

/*************************\
* HARTLEY MODULATOR (SSB) *
\*************************/

/*
 * For more information on the Hartley SSB modulator visit
 * http://dp.nonoo.hu/projects/ham-dsp-tutorial/09-ssb-hartley/
 *
 * This one is simpler and it creates a nice SSB spectrum, however
 * the FIR filter used here to perform the Hilbert transform (an
 * asymetric FIR filter) is not steep enough and frequencies close
 * to the center don't have propper phase difference.  We can't have
 * a better FIR filter here because we need lots of taps (I tried
 * with 500+ and it still didn't work well) and this runs at the
 * sample rate of the oscilator so we can't afford it.
 */
static float
get_ssb_hartley_sample(struct fmmod_instance *fmmod, float* lpr, float* lmr,
		       int num_samples, int idx)
{
	static float delay_line[HT_FIR_FILTER_HALF_SIZE] = { 0 };
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	struct fmmod_control *ctl = fmmod->ctl;
	float shifted_sample = 0.0;
	float out = 0.0;
	float tmp = 0.0;
	int carrier_freq = 38000;
	int i = 0;

	/* Delayed L + R */
	out = get_delayed_lpr_sample(fmmod, lpr, num_samples,
				     HT_FIR_FILTER_HALF_SIZE, idx);

	/* Stereo pilot at 19KHz */
	out += ctl->pilot_gain * osc_get_19Khz_sample(sin_osc);

	/* Put the original sample on the end of the delay line */
	for (i = 0; i < HT_FIR_FILTER_HALF_SIZE - 1; i++)
		delay_line[i] = delay_line[i + 1];
	delay_line[HT_FIR_FILTER_HALF_SIZE - 1] = lmr[idx];

	/* Phase shift by 90deg using the Hilbert transformer */
	shifted_sample = hilbert_transformer_apply(&fmmod->ht, lmr[idx]);

	/* Phase lock the ssb oscilator to the master
	 * oscilator */
	cos_osc->current_phase = sin_osc->current_phase;

	/* The begining of the delay line and the shifted sample
	 * now have a 90deg phase difference, oscilate one with
	 * the master -sine- oscilator and the other with the ssb
	 * -cosine- oscilator (90deg difference) and shift the
	 * modulated signal to the carrier freq */
	tmp = shifted_sample * osc_get_sample_for_freq(sin_osc, carrier_freq);
	tmp += delay_line[0] * osc_get_sample_for_freq(cos_osc, carrier_freq);

	/* Set the output gain percentage */
	out += tmp * ctl->stereo_carrier_gain;

	return out;
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
	int i;
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
	int frames_generated;
	struct fmmod_instance *fmmod = (struct fmmod_instance *)arg;
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct resampler_data *rsmpl = &fmmod->rsmpl;
	struct audio_filter *aflt = &fmmod->aflt;
	struct fmmod_control *ctl = fmmod->ctl;
	mpx_generator get_mpx_sample;

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
	for (i = 0; i < nframes; i++) {
		fmmod->inbuf_l[i] = ctl->audio_gain *
		    audio_filter_apply(aflt, left_in[i], 0, ctl->use_audio_lpf);

		fmmod->inbuf_r[i] = ctl->audio_gain *
		    audio_filter_apply(aflt, right_in[i], 1, ctl->use_audio_lpf);

		/* Update audio peak levels */
		if (fmmod->inbuf_l[i] > ctl->peak_audio_in_l)
			ctl->peak_audio_in_l = fmmod->inbuf_l[i];

		if (fmmod->inbuf_r[i] > ctl->peak_audio_in_r)
			ctl->peak_audio_in_r = fmmod->inbuf_r[i];

		audio_filter_update(aflt);
	}

	/* Upsample to the sample rate of the main oscilator */
	frames_generated = resampler_upsample_audio(rsmpl, fmmod->inbuf_l,
						    fmmod->inbuf_r,
						    upsampled_audio_l,
						    upsampled_audio_r,
						    nframes,
						    fmmod->uaudio_buf_len);
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
		get_mpx_sample = get_mono_sample;
		break;
	case FMMOD_SSB_HARTLEY:
		get_mpx_sample = get_ssb_hartley_sample;
		break;
	case FMMOD_SSB_WEAVER:
		get_mpx_sample = get_ssb_weaver_sample;
		break;
	case FMMOD_SSB_FIR:
		get_mpx_sample = get_ssb_fir_sample;
		break;
	case FMMOD_DSB:
	default:
		get_mpx_sample = get_dsb_sample;
		break;
	}

	/* Create the multiplex signal */
	for (i = 0; i < frames_generated; i++) {
		/* Get Mono/Stereo encoded MPX signal */
		mpxbuf[i] = (*get_mpx_sample) (fmmod,
						lpr_buf, lmr_buf,
						frames_generated,
						i);

		/* Add RDS symbols modulated by the 57KHz carrier (3 x Pilot) */
		mpxbuf[i] += ctl->rds_gain * osc_get_57Khz_sample(sin_osc) *
					rds_get_next_sample(&fmmod->rds_enc);

		/* Set mpx gain percentage */
		mpxbuf[i] *= ctl->mpx_gain;

		osc_increase_phase(sin_osc);
	}

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
		fprintf(stderr,
			"jack_client_open() failed, status = 0x%2.0x\n",
			status);
		if (status & JackServerFailed)
			fprintf(stderr, "Unable to connect to JACK server\n");
		return FMMOD_ERR_JACKD_ERR;
	}

	if (status & JackServerStarted)
		fprintf(stderr, "JACK server started\n");

	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(fmmod->client);
		fprintf(stderr, "Unique name `%s' assigned\n", client_name);
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
		fprintf(stderr, "got invalid samplerate from jackd\n");
		return FMMOD_ERR_JACKD_ERR;
	}

	/* Get maximum number of frames JACK will send to process() */
	max_process_frames = jack_get_buffer_size(fmmod->client);

	/* Allocate input audio buffers */
	fmmod->inbuf_len = max_process_frames *
	    		   sizeof(jack_default_audio_sample_t);
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
	fmmod->uaudio_buf_len = (uint32_t)
	    ((((float)osc_samplerate /
	       (float)jack_samplerate) + 1.0) *
	     (float)max_process_frames *
	     (float)sizeof(jack_default_audio_sample_t));

	fmmod->uaudio_buf_0 = (float *)malloc(fmmod->uaudio_buf_len);
	if (fmmod->uaudio_buf_0 == NULL) {
		ret = -1;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_0, 0, fmmod->uaudio_buf_len);

	fmmod->uaudio_buf_1 = (float *)malloc(fmmod->uaudio_buf_len);
	if (fmmod->uaudio_buf_1 == NULL) {
		ret = -1;
		goto cleanup;
	}
	memset(fmmod->uaudio_buf_1, 0, fmmod->uaudio_buf_len);

	/* Allocate output buffer for MPX */
	fmmod->mpxbuf_len = (uint32_t)
	    ((((float)osc_samplerate /
	       (float)jack_samplerate) + 1.0) *
	     (float)max_process_frames *
	     (float)sizeof(jack_default_audio_sample_t));
	fmmod->mpxbuf = (float *)malloc(fmmod->mpxbuf_len);
	if (fmmod->mpxbuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->mpxbuf, 0, fmmod->mpxbuf_len);

	/* Initialize the main oscilator */
	ret = osc_initialize(&fmmod->sin_osc, osc_samplerate, OSC_TYPE_SINE);
	if (ret < 0) {
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the cosine oscilator of the weaver modulator */
	ret = osc_initialize(&fmmod->cos_osc, osc_samplerate, OSC_TYPE_COSINE);
	if (ret < 0) {
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the low pass FIR filter for the FIR-based SSB modulator */
	fir_filter_init(&fmmod->ssb_fir_lpf, 37500, osc_samplerate);
	/* Initialize the low pass filters of the Weaver modulator */
	iir_ssb_filter_init(&fmmod->weaver_lpf);
	/* Initialize the Hilbert transformer for the Hartley modulator */
	hilbert_transformer_init(&fmmod->ht);

	/* Initialize resampler */
	ret = resampler_init(&fmmod->rsmpl, jack_samplerate,
			     osc_samplerate,
			     RDS_SAMPLE_RATE,
			     output_samplerate, max_process_frames);
	if (ret < 0) {
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
	audio_filter_init(&fmmod->aflt, 16000, jack_samplerate, preemph_usecs);

	/* Initialize RDS encoder */
	ret = rds_encoder_init(&fmmod->rds_enc, &fmmod->rsmpl);
	if (ret < 0) {
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
					JackPortIsInput, 0);
	if (fmmod->inL == NULL) {
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	fmmod->inR = jack_port_register(fmmod->client, "AudioR",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput, 0);
	if (fmmod->inR == NULL) {
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
		perror("mkfifo()");
		goto cleanup;
	}

	/* Register output port if possible, if not allocate
	 * our own buffers since we can't use JACK's */
	if (fmmod->output_type == FMMOD_OUT_JACK) {
		fmmod->outMPX = jack_port_register(fmmod->client, "MPX",
						   JACK_DEFAULT_AUDIO_TYPE,
						   JackPortIsOutput, 0);
		if (fmmod->outMPX == NULL) {
			ret = FMMOD_ERR_JACKD_ERR;
			goto cleanup;
		}
	} else {
		fmmod->sock_outbuf_len = (uint32_t)
		    (((float)output_samplerate /
		      (float)osc_samplerate) * (float)fmmod->mpxbuf_len);
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
		ret = FMMOD_ERR_RTP_ERR;
		goto cleanup;
	}

	/* Initialize the control I/O channel */
	fmmod->ctl_map = utils_shm_init(FMMOD_CTL_SHM_NAME,
					sizeof(struct fmmod_control));
	if (!fmmod->ctl_map) {
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
	if (ret != 0)
		ret = FMMOD_ERR_JACKD_ERR;
	else
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

	utils_shm_destroy(fmmod->ctl_map, 1);

	rds_encoder_destroy(&fmmod->rds_enc);

	resampler_destroy(&fmmod->rsmpl);

	rtp_server_destroy(&fmmod->rtpsrv);

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

	return;
}
