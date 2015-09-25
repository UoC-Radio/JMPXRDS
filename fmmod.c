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

#include <jack/transport.h>
#include <stdlib.h> /* For malloc() */
#include <string.h> /* For memset() */
#include <stdio.h> /* For printf */
#include <sys/mman.h>	/* For shm_open */
#include <sys/stat.h>	/* For mode constants */
#include <fcntl.h>	/* For O_* constants */
#include "fmmod.h"

/*********\
* HELPERS *
\*********/

static inline float
get_am_sample(struct osc_state *sin_osc, float sample)
{
	return sample * osc_get_38Khz_sample(sin_osc);
}

/*
 * Some notes on SSB modulation for the stereo subcarrier
 * http://ham-radio.com/k6sti/ssb.htm
 * http://wheatstone.com/index.php/corporate-support/all-downloads/
 * doc_download/502-new-findings-on-fm-stereo-multipath-control
 *
 * In my tests with both SSB modulators I got no stereo
 * separation from my receiver (I also tried using a simple
 * low pass filter so it doesn't have to do with the modulation
 * it's the receiver that couldn't decode stereo -reduced the 
 * stereo separation-). I did get better RDS reception with RDS
 * Spy and better coverage though. It seems that many receivers
 * will misbehave so don't use SSB because it's "fancy". It's better
 * than mono but the standard subcarrier performs better.
 */


/************************\
* WEAVER MODULATOR (SSB) *
\************************/

/*
 * For more infos on the Weaver SSB modulator visit
 * http://dp.nonoo.hu/projects/ham-dsp-tutorial/11-ssb-weaver/
 *
 * Since the IIR filter is not good enough this is mostly a
 * VSB modulator plus the IIR filter messes up with the phase.
 * A FIR filter would need more taps to be steep enough and
 * is not practical since this runs on the oscilator's
 * sample rate. I left this here for reference and experimentation.
 * On my tests my -probably incompatible- receiver switched to
 * mono sometimes.
 */

static float
get_ssb_weaver_sample(struct fmmod_instance *fmmod, float sample)
{
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	double saved_sin_phase = 0;
	float in_phase = 0;
	float quadrature = 0;
	float out = 0;
	int frequency_shift = 0;
	int i = 0;

	/* Phase lock the ssb oscilator to the master
	 * oscilator */
	cos_osc->current_phase = sin_osc->current_phase;

	/* Create a quadrature version of the input signal */
	in_phase = sample * osc_get_sample_for_freq(sin_osc,
					sin_osc->sample_rate / 4);
	quadrature = sample * osc_get_sample_for_freq(cos_osc,
					cos_osc->sample_rate / 4);

	/* Apply the low pass filter */
	in_phase = iir_ssb_filter_apply(&fmmod->ssb_lpf, in_phase, 0);
	quadrature = iir_ssb_filter_apply(&fmmod->ssb_lpf, quadrature, 1);

	/* Since the IIR LPF filter above adds a delay of up to
	 * SSB_FILTER_TAPS increase the phase of ssb_osc to match
	 * the phase of the current output samples */
	for(i = 0; i < SSB_FILTER_TAPS; i++)
		osc_increase_phase(cos_osc);

	/* Save master oscilator's phase */
	saved_sin_phase = sin_osc->current_phase;

	/* Phase-sync the two modulators (they should have
	 * exactly 90deg phase difference. One is sine, the other
	 * is cosine so for the same angle -phase- they will have
	 * 90deg phase difference as needed) */
	sin_osc->current_phase = cos_osc->current_phase;

	/* Shift it to the carrier frequency and combine in_phase
	 * and quadrature to create the LSB signal we want */
	frequency_shift = (sin_osc->sample_rate / 4) - 38000;
	in_phase *= osc_get_sample_for_freq(sin_osc,
					frequency_shift);
	frequency_shift = (cos_osc->sample_rate / 4) - 38000;
	quadrature *= osc_get_sample_for_freq(cos_osc,
					frequency_shift);
	
	out = in_phase + quadrature;

	/* Restore master oscilator's phase */
	sin_osc->current_phase = saved_sin_phase;

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
 * sample rate of the oscilator so we can't afford it. An
 * FFT/IFFT based Hilbert Transformer should work better.
 * I left this one as the default because at least with this
 * one the receiver didn't switch to mono.
 */
static float
get_ssb_hartley_sample(struct fmmod_instance *fmmod, float sample)
{
	static float delay_line[HT_FIR_FILTER_TAPS / 2 + 1] = {0};
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct osc_state *cos_osc = &fmmod->cos_osc;
	float shifted_sample = 0;
	float out = 0;
	int carrier_freq = 38000;
	int i = 0;

	/* Put the original sample on the end of the delay line */
	for(i = 0; i < HT_FIR_FILTER_TAPS / 2; i++)
		delay_line[i] = delay_line[i + 1];
	delay_line[HT_FIR_FILTER_TAPS / 2] = sample;

	/* Phase shift by 90deg using the Hilbert transformer */
	shifted_sample = hilbert_transformer_apply(&fmmod->ht, sample);

	/* Phase lock the ssb oscilator to the master
	 * oscilator */
	cos_osc->current_phase = sin_osc->current_phase;

	/* The begining of the delay line and the shifted sample
	 * now have a 90deg phase difference, oscilate one with
	 * the master -sine- oscilator and the other with the ssb
	 * -cosine- oscilator (90deg difference) and shift the
	 * modulated signal to the carrier freq */
	out = shifted_sample * osc_get_sample_for_freq(sin_osc, carrier_freq);
	out += delay_line[0] * osc_get_sample_for_freq(cos_osc, carrier_freq);

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
	int i, c, ret;
	jack_default_audio_sample_t *left_in, *right_in;
	jack_default_audio_sample_t *mpx_out;
	jack_default_audio_sample_t *left_out, *right_out;
	float *mpxbuf = NULL;
	float *upsampled_audio = NULL;
	int frames_generated;
	struct fmmod_instance *fmmod = (struct fmmod_instance *) arg;
	struct osc_state *sin_osc = &fmmod->sin_osc;
	struct resampler_data *rsmpl = &fmmod->rsmpl;
	struct audio_filter *aflt = &fmmod->aflt;

	left_in = jack_port_get_buffer(fmmod->inL, nframes);
	right_in = jack_port_get_buffer(fmmod->inR, nframes);
	mpx_out = jack_port_get_buffer(fmmod->outMPX, nframes);
	mpxbuf = fmmod->mpxbuf;

	/* Apply audio filter on input and merge the two
	 * channels to prepare the buffer for upsampling */
	for(i = 0, c = 0; i < nframes; i++) {
		fmmod->ioaudiobuf[c] = audio_filter_apply(aflt,
						left_in[i], 0);
		fmmod->ioaudiobuf[c + 1] = audio_filter_apply(aflt,
						right_in[i], 1);
		audio_filter_update(aflt);
		c += 2;
	}

	/* Upsample to the sample rate of the main oscilator */
	upsampled_audio = resampler_upsample_audio(rsmpl, fmmod->ioaudiobuf,
								nframes, &ret);
	if(upsampled_audio == NULL)
		return FMMOD_ERR_RESAMPLER_ERR;

	frames_generated = ret;

	/* Create the multiplex signal */
	for(i = 0, c = 0; i < frames_generated; i++) {
		/* L + R */
		mpxbuf[i] = 0.45 * (upsampled_audio[c] +
				upsampled_audio[c + 1]);

		/* 19KHz FM Stereo Pilot */
		mpxbuf[i] += 0.08 * osc_get_19Khz_sample(sin_osc);

		/* L - R, AM modulated with 38KHz carrier (2 x Pilot) */
		if(fmmod->enable_ssb)
			mpxbuf[i] += 0.45 * get_ssb_hartley_sample(fmmod,
						upsampled_audio[c] -
						upsampled_audio[c + 1]);
		else
			mpxbuf[i] += 0.45 * get_am_sample(sin_osc,
						upsampled_audio[c] -
						upsampled_audio[c + 1]);

		/* RDS symbols multiplied by the RDS Carrier (3 x Pilot) */
		mpxbuf[i] += 0.02 * osc_get_57Khz_sample(sin_osc) *
				rds_get_next_sample(fmmod->enc);
		c += 2;
		osc_increase_phase(sin_osc);
	}


	/* Now downsample back to jack's sample rate for output */
	mpx_out = resampler_downsample_mpx(rsmpl, mpxbuf, mpx_out,
					frames_generated, &ret);
	if(mpx_out == NULL)
		return FMMOD_ERR_RESAMPLER_ERR;

	return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
static void
fmmod_shutdown(void *arg)
{
	struct fmmod_instance *fmmod = (struct fmmod_instance *) arg;
	fmmod_destroy(fmmod);
	exit(0);
}


/****************\
* INIT / DESTROY *
\****************/

int
fmmod_initialize(struct fmmod_instance *fmmod, int region)
{
	int ret = 0;
	int jack_samplerate = 0;
	int max_process_frames = 0;
	int preemph_usecs = 0;
	int rds_enc_fd = 0;
	char *client_name = NULL;
	jack_options_t options = JackNoStartServer;
	jack_status_t status;

	memset(fmmod, 0, sizeof(struct fmmod_instance));

   	/* Open a client connection to the default JACK server */
	fmmod->client = jack_client_open("FMmod", options, &status, NULL);
	if(fmmod->client == NULL) {
		fprintf(stderr,
			"jack_client_open() failed, status = 0x%2.0x\n",
			status);
		if(status & JackServerFailed)
			fprintf(stderr, "Unable to connect to JACK server\n");
		return FMMOD_ERR_JACKD_ERR;
	}

	if(status & JackServerStarted)
		fprintf(stderr, "JACK server started\n");

	if(status & JackNameNotUnique) {
		client_name = jack_get_client_name(fmmod->client);
		fprintf(stderr, "unique name `%s' assigned\n", client_name);
	}

	/* Get JACK's sample rate */
	jack_samplerate = jack_get_sample_rate(fmmod->client);

	/* Get maximum number of frames JACK will send to process() */
	max_process_frames = jack_get_buffer_size(fmmod->client);

	/* Allocate buffers */
	fmmod->ioaudiobuf_len = 2 * max_process_frames *
				sizeof(jack_default_audio_sample_t);
	fmmod->ioaudiobuf = (float*) malloc(fmmod->ioaudiobuf_len);
	if(fmmod->ioaudiobuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->ioaudiobuf, 0, fmmod->ioaudiobuf_len);

	fmmod->mpxbuf_len = fmmod->ioaudiobuf_len;
	fmmod->mpxbuf = (float*) malloc(fmmod->mpxbuf_len);
	if(fmmod->mpxbuf == NULL) {
		ret = FMMOD_ERR_NOMEM;
		goto cleanup;
	}
	memset(fmmod->mpxbuf, 0, fmmod->mpxbuf_len);

	/* Initialize the main oscilator */
	ret = osc_initialize(&fmmod->sin_osc, OSC_SAMPLE_RATE,
						OSC_TYPE_SINE);
	if(ret < 0) {
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the cosine oscilator of the weaver modulator */
	ret = osc_initialize(&fmmod->cos_osc, OSC_SAMPLE_RATE,
						OSC_TYPE_COSINE);
	if(ret < 0) {
		ret = FMMOD_ERR_OSC_ERR;
		goto cleanup;
	}

	/* Initialize the low pass filters of the Weaver modulator */
	iir_ssb_filter_init(&fmmod->ssb_lpf);
	/* Initialize the Hilbert transformer for the Hartley modulator */
	hilbert_transformer_init(&fmmod->ht);
	fmmod->enable_ssb = 0;

	/* Initialize resampler */
	ret = resampler_init(&fmmod->rsmpl, jack_samplerate,
					fmmod->sin_osc.sample_rate,
					max_process_frames);
	if(ret < 0) {
		ret = FMMOD_ERR_RESAMPLER_ERR;
		goto cleanup;
	}


	/* Initialize audio filter */
	switch(region) {
	case REGION_US:
		preemph_usecs = 75;
		break;
	case REGION_EU:
	case REGION_WORLD:
		preemph_usecs = 50;
		break;
	default:
		preemph_usecs = 50;
		break;
	}
	audio_filter_init(&fmmod->aflt, 16000, jack_samplerate,
						preemph_usecs);

	/* Initialize RDS encoder */
	rds_enc_fd = shm_open(RDS_ENC_SHM_NAME, O_CREAT|O_RDWR, 0600);
	if(rds_enc_fd < 0) {
		ret = FMMOD_ERR_SHM_ERR;
		goto cleanup;
	}

	ret = ftruncate(rds_enc_fd, sizeof(struct rds_encoder));
	if(ret != 0) {
		ret = FMMOD_ERR_SHM_ERR;
		goto cleanup;
	}


	fmmod->enc = mmap(0, sizeof(struct rds_encoder),
				PROT_READ | PROT_WRITE, MAP_SHARED,
				rds_enc_fd, 0);
	if(fmmod->enc == MAP_FAILED) {
		ret = FMMOD_ERR_SHM_ERR;
		goto cleanup;
	}
	close(rds_enc_fd);

	ret = rds_encoder_init(fmmod->enc, OSC_SAMPLE_RATE);
	if(ret < 0) {
		ret = FMMOD_ERR_RDS_ERR;
		goto cleanup;
	}

	/* Register callbacks on JACK */
	jack_set_process_callback(fmmod->client, fmmod_process, fmmod);
	jack_on_shutdown(fmmod->client, fmmod_shutdown, fmmod);


	/* Register ports */
	fmmod->inL = jack_port_register(fmmod->client, "AudioL",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput, 0);
	if(fmmod->inL == NULL) {
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	fmmod->inR = jack_port_register(fmmod->client, "AudioR",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsInput, 0);
	if(fmmod->inR == NULL) {
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	fmmod->outMPX = jack_port_register(fmmod->client, "MPX",
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsOutput, 0);
	if(fmmod->outMPX == NULL) {
		ret = FMMOD_ERR_JACKD_ERR;
		goto cleanup;
	}

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */
	ret = jack_activate(fmmod->client);
	if(ret != 0)
		ret = FMMOD_ERR_JACKD_ERR;

cleanup:
	if(ret < 0) {
		fmmod_destroy(fmmod);
		return ret;
	} else
		return 0;
}

void
fmmod_destroy(struct fmmod_instance *fmmod)
{
	if(fmmod->ioaudiobuf != NULL)
		free(fmmod->ioaudiobuf);
	if(fmmod->mpxbuf != NULL)
		free(fmmod->mpxbuf);
	resampler_destroy(&fmmod->rsmpl);
	rds_encoder_destroy(fmmod->enc);
	munmap(fmmod->enc, sizeof(struct rds_encoder));
	jack_client_close(fmmod->client);
}