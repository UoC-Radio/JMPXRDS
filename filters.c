/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Various filter implementations
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
#include "filters.h"
#include <stdlib.h>		/* For NULL */
#include <string.h>		/* For memset */
#include <math.h>		/* For exp() */
#include <jack/thread.h>	/* For thread handling through jack */

/*********\
* HELPERS *
\*********/

/* Note that we don't use the normalization factor
 * here so that it always starts at 1.0 -exp(0)-. */
static float
gaussian(uint16_t bin, float variance)
{
	return (float) exp(-1.0L * pow((double) bin, 2) / (2.0L * (double) variance));
}

static float
bin2freq(struct lpf_filter_data *lpf, uint16_t bin)
{
	float bratio = 0;
	float freq = 0;
	float nyquist_freq = 0;
	nyquist_freq = (float) lpf->sample_rate / 2.0;
	bratio = (float) bin / (float) lpf->middle_bin;
	freq = bratio * nyquist_freq;
	return freq;
}

static uint16_t
freq2bin(struct lpf_filter_data *lpf, float freq)
{
	float fratio = 0;
	float nyquist_freq = 0;
	uint16_t bin = 0;
	nyquist_freq = (float) lpf->sample_rate / 2.0;
	fratio = freq / nyquist_freq;
	bin = (uint16_t) (fratio * (float) lpf->middle_bin);
	return bin;
}


/******************************\
* FM PRE-EMPHASIS AUDIO FILTER *
\******************************/

/*
 * The FM Pre-emphasis filter is an RC high-shelf filter with
 * one pole tap and one zero tap. After the zero, the filter's Bode diagram
 * gives a 20dB/decade until we reach the pole, which means that when the
 * frequency is multiplied by 10 the gain is increased by 20dB. If we make
 * this mapping between amplitude and frequency we'll get (1, 10^(0/20)),
 * (10, 10^(20/20)), (100, 10^(40/20)), (1000, 10^(60/20))...
 * This becomes (1,1), (10,10), (100, 100), (1000, 1000)... which is a
 * straight line, so the response is a linear function of the frequency.
 *
 * The cutoff frequency is calculated from the time constant of the analog
 * RC filter (tau) and is different on US (75us) and EU (50us) radios.
 *
 * For more information check out:
 * https://www.radiomuseum.org/forum/fm_pre_emphasis_and_de_emphasis.html
 */


static int
fmpreemph_filter_init_mode(struct fmpreemph_filter_data *fmprf,
			   float sample_rate,
			   float high_corner_freq,
			   uint8_t preemph_tau_usecs)
{
	double tau = 0.000001 * (double) preemph_tau_usecs;

	/* Corner angular frequencies (w -> omega) */
	/* t = R*C = 1 / wc
	 * wc = 1 / t */
	double cutoff_w_low = 1.0L / tau;
	double cutoff_w_high = 2.0L * M_PI * (double) high_corner_freq;

	/* Corner angular frequencies relative to the sampling rate */
	double pre_warped_wc = tan(cutoff_w_low /  (2.0L * (double) sample_rate));
	double pre_warped_wh = tan(cutoff_w_high / (2.0L * (double) sample_rate));

	/* V0 = 10^gain/20, however as we saw above that
	 * increases proportionaly with the frequency so
	 * for a given high corner frequency we are going
	 * to get an increase in gain of high_corner / low_corner */
	double V0 = log10(cutoff_w_high / cutoff_w_low);
	double H0 = V0 - 1;

	double B = V0 * pre_warped_wc;

	/* Forward taps */
	double ataps_0 = 1.0L;
	double ataps_1 = (B - 1) / (B + 1);

	/* Backwards taps */
	double btaps_0 = (1.0L + (1.0L - ataps_1) * H0 / 2.0L);
	double btaps_1 = (ataps_1 + (ataps_1 - 1.0L) * H0 / 2.0L);

	switch (preemph_tau_usecs) {
		case 50:
			fmprf->ataps_50[0] = (float) ataps_0;
			fmprf->ataps_50[1] = (float) ataps_1;

			fmprf->btaps_50[0] = (float) btaps_0;
			fmprf->btaps_50[1] = (float) btaps_1;
			break;
		case 75:
			fmprf->ataps_75[0] = (float) ataps_0;
			fmprf->ataps_75[1] = (float) ataps_1;

			fmprf->btaps_75[0] = (float) btaps_0;
			fmprf->btaps_75[1] = (float) btaps_1;
			break;
		default:
			return -1;
	}

	return 0;
}

static int
fmpreemph_filter_init(struct fmpreemph_filter_data *fmprf,
		      float sample_rate,
		      float high_corner_freq)
{
	int ret = 0;

	ret = fmpreemph_filter_init_mode(fmprf,
			   sample_rate,
			   high_corner_freq,
			   50);
	if (ret < 0)
		return ret;

	ret = fmpreemph_filter_init_mode(fmprf,
			   sample_rate,
			   high_corner_freq,
			   75);
	if (ret < 0)
		return ret;
}

static float
fmpreemph_filter_apply(struct fmpreemph_filter_data *fmprf,
		       float sample, enum lpf_preemph_mode tau_mode)
{
	float out = 0.0;
	float *ataps = NULL;
	float *btaps = NULL;
	static int prev_tau_mode = LPF_PREEMPH_NONE;

	switch (tau_mode) {
		case LPF_PREEMPH_NONE:
			return sample;

		case LPF_PREEMPH_75US:
			ataps = fmprf->ataps_75;
			btaps = fmprf->btaps_75;
			break;

		case LPF_PREEMPH_50US:
		default:
			ataps = fmprf->ataps_50;
			btaps = fmprf->btaps_50;
			break;
	};

	out += ataps[0] * sample;

	/* When switching modes don't use the previous
	 * input/output. */
	if (prev_tau_mode != tau_mode) {
		fmprf->last_in = sample;
		fmprf->last_out[0] = out;
		prev_tau_mode = tau_mode;
		return out;
	}

	out += ataps[1] * fmprf->last_in;

	fmprf->last_in = sample;

	out += btaps[0] * fmprf->last_out[0];
	out += btaps[1] * fmprf->last_out[1];

	fmprf->last_out[1] = fmprf->last_out[0];
	fmprf->last_out[0] = out;

	return out;
}

/*****************************\
* GENERIC FFT LOW-PASS FILTER *
\*****************************/

void
lpf_filter_destroy(struct lpf_filter_data *lpf)
{
	if(lpf->filter_curve)
		free(lpf->filter_curve);
	if(lpf->real_in)
		fftwf_free(lpf->real_in);
	if(lpf->real_out)
		fftwf_free(lpf->real_out);
	if(lpf->complex_buff)
		fftwf_free(lpf->complex_buff);
	if(lpf->dft_plan)
		fftwf_destroy_plan(lpf->dft_plan);
	if(lpf->ift_plan)
		fftwf_destroy_plan(lpf->ift_plan);
}

int
lpf_filter_init(struct lpf_filter_data *lpf, uint32_t cutoff_freq,
		uint32_t sample_rate, uint16_t max_frames,
		uint8_t overlap_factor)
{
	int i = 0;
	int ret = 0;
	float trans_bw = 0.0;
	uint16_t remaining_bins = 0;
	float nyquist_freq = 0.0;

	/* Initialize filter parameters */
	lpf->period_size = max_frames;
	lpf->num_bins = overlap_factor * lpf->period_size;
	lpf->sample_rate = sample_rate;
	lpf->middle_bin = (lpf->num_bins / 2) + 1;
	nyquist_freq = (float) lpf->sample_rate / 2.0;
	lpf->bin_bw = (nyquist_freq / (float) lpf->num_bins);
	lpf->overlap_len = lpf->num_bins - lpf->period_size;

	/*
	 * We don't want the filter to bee too steep (like a sinc filter which
	 * zeroes out everything after the cutoff bin) to avoid the "ringing"
	 * noise. Instead use a Gaussian curve (the second half part of the
	 * "bell") to make the transition smooth. As a bonus, the gausian
	 * remains the same when switching to the frequency domain and back
	 * and is real in both cases, which means we don't have to store
	 * the filter's responce in complex form.
	 *
	 * We calculate the Gaussian's variance -the witdh of the bell- for
	 * a given transition bandwidth, in bins, the same way we did it above
	 * for the cutoff frequency bin.
	 */
	trans_bw = 2500.0 / lpf->bin_bw; /* 2.5KHz should be enough */
	lpf->variance = 2.0 * trans_bw;

	/*
	 * Calculate the frequency bin after which we 'll start filtering out
	 * the remaining bins. The frequency bins will cover frequencies from
	 * 0 to Nyquist frequency so we have to calculate what portion of that
	 * spectrum is our passband (below the cutoff frequency), relative to
	 * the Nyquist frequency.
	 *
	 * Note: Because the signal is purely real, the FFT be "mirrored"
	 * -conjugate symmetric-, so we only need to use half of it (positive
	 * frequencies) plus the point in the middle. That's why we have half
	 * the bins (lpf->middle_bin - 0) here and below.
	 */
	lpf->cutoff_bin = freq2bin(lpf, cutoff_freq);

	/* Calculate and store the filter's FFT curve from cutoff_bin
	 * to the end of the spectrum */
	remaining_bins = lpf->middle_bin - lpf->cutoff_bin;
	lpf->filter_curve = (float*) malloc(remaining_bins * sizeof(float));
	if(!lpf->filter_curve) {
		ret = -1;
		goto cleanup;
	}

	for(i = 0; i < remaining_bins; i++)
		lpf->filter_curve[i] = gaussian(i, lpf->variance);


	/* Allocate buffers for DFT/IFT */
	lpf->real_in = fftwf_alloc_real(lpf->num_bins);
	if(!lpf->real_in) {
		ret = -2;
		goto cleanup;
	}
	memset(lpf->real_in, 0, lpf->num_bins * sizeof(float));

	lpf->real_out = fftwf_alloc_real(lpf->num_bins);
	if(!lpf->real_out) {
		ret = -3;
		goto cleanup;
	}
	memset(lpf->real_out, 0, lpf->num_bins * sizeof(float));

	lpf->complex_buff = fftwf_alloc_complex(lpf->middle_bin - 0);
	if(!lpf->complex_buff) {
		ret = -4;
		goto cleanup;
	}
	memset(lpf->complex_buff, 0, sizeof(fftwf_complex) *
					(lpf->middle_bin - 0));


	/* Create DFT plan */
	lpf->dft_plan = fftwf_plan_dft_r2c_1d(lpf->num_bins, lpf->real_in,
					     lpf->complex_buff, FFTW_MEASURE);
	if(!lpf->dft_plan) {
		ret = -5;
		goto cleanup;
	}


	/* Create IFT plan */
	lpf->ift_plan = fftwf_plan_dft_c2r_1d(lpf->num_bins, lpf->complex_buff,
					     lpf->real_out, FFTW_MEASURE);
	if(!lpf->ift_plan)
		ret = -6;

 cleanup:
	if(ret < 0)
		lpf_filter_destroy(lpf);
	return ret;
}

int
lpf_filter_apply(struct lpf_filter_data *lpf, float *in, float *out,
		 uint16_t num_samples, float gain)
{
	float tau = 0.0;
	float ratio = 0.0;
	float fc = 0.0;
	uint16_t preemph_start_bin = 0;
	float pe_resp = 0.0;
	float bin_bw_scaled = 0.0;
	int i = 0;
	int c = 0;

	/* Shift the buffer's content to make room for the new
	 * period on its end and then put the new data there. */
	memmove(lpf->real_in, lpf->real_in + lpf->period_size,
		lpf->overlap_len * sizeof(float));
	memcpy(lpf->real_in + lpf->overlap_len, in,
		num_samples * sizeof(float));

	/* Run the DFT plan to get the freq domain (complex or
	 * analytical) representation of the signal */
	fftwf_execute(lpf->dft_plan);

	/* Now signal is on the complex buffer and filter-out all
	 * frequency bins above cutoff_bin */

	/* Multiply the input signal -after the cutoff bin- with the filter's
	 * response. */
	for(i = lpf->cutoff_bin, c = 0; i < lpf->middle_bin; i++, c++) {
		lpf->complex_buff[i][0] *= lpf->filter_curve[c];
		lpf->complex_buff[i][1] *= lpf->filter_curve[c];
	}

	/* Switch the signal back to the time domain */
	fftwf_execute(lpf->ift_plan);

	/* Note that FFTW returns unnormalized data so the IFT output
	 * is multiplied with the product of the logical dimentions
	 * which in our case is bins.
	 * To make things simpler and more efficient, we calculate a gain
	 * ratio that will handle both the requested gain and the
	 * normalization (multiplication is cheaper than division). */
	ratio = (float) gain / (float) lpf->num_bins;

	/* Output the begining of the real_out buffer and discard the overlap
	 * that follows */
	for(i = 0; i < num_samples; i++)
		out[i] = lpf->real_out[i] * ratio;

	return 0;
}


/***********************************************\
* COMBINED AUDIO FILTER (FM PRE-EMPHASIS + LPF) *
\***********************************************/

static void
audio_filter_thread_run(struct aflt_thread_data *afltd)
{
	int i = 0;
	float tmp_gain = 0;

	/* If pre-emphasis is requested, run the input buffer through
	 * the pre-emphasis filter in the time domain before feeding
	 * it to the lpf. Re-use the output buffer. */
	if (afltd->preemph_tau_mode != LPF_PREEMPH_NONE) {
		for(i = 0; i < afltd->num_samples; i++)
			afltd->out[i] = fmpreemph_filter_apply(afltd->fmprf,
							afltd->in[i],
							afltd->preemph_tau_mode) * 8.0;

		afltd->result = lpf_filter_apply(afltd->lpf, afltd->out, afltd->out,
						afltd->num_samples, afltd->gain);
	} else
		afltd->result = lpf_filter_apply(afltd->lpf, afltd->in, afltd->out,
						afltd->num_samples, afltd->gain);

	/* Update audio peak levels */
	for(i = 0, tmp_gain = 0; i < afltd->num_samples; i++)
		if(afltd->out[i] > tmp_gain)
			tmp_gain = afltd->out[i];

	afltd->peak_gain = tmp_gain;
}

#ifdef JMPXRDS_MT
static void*
audio_filter_loop(void* arg)
{
	struct aflt_thread_data *afltd = (struct aflt_thread_data*) arg;

	while((*afltd->active)) {
		pthread_mutex_lock(&afltd->proc_mutex);
		while (pthread_cond_wait(&afltd->proc_trigger, &afltd->proc_mutex) != 0);

		if(!(*afltd->active))
			break;

		audio_filter_thread_run(afltd);

		pthread_mutex_unlock(&afltd->proc_mutex);

		/* Let the caller know we are done */
		pthread_mutex_lock(&afltd->done_mutex);
		pthread_cond_signal(&afltd->done_trigger);
		pthread_mutex_unlock(&afltd->done_mutex);
	}

	return arg;
}
#endif

void
audio_filter_destroy(struct audio_filter *aflt)
{
	lpf_filter_destroy(&aflt->audio_lpf_l);
	lpf_filter_destroy(&aflt->audio_lpf_r);
}

int
audio_filter_init(struct audio_filter *aflt, jack_client_t *fmmod_client,
		  uint32_t cutoff_freq, uint32_t sample_rate,
		  uint16_t max_samples)
{
	struct aflt_thread_data *afltd_l = &aflt->afltd_l;
	struct aflt_thread_data *afltd_r = &aflt->afltd_r;
	int rtprio = 0;
	int ret = 0;
	aflt->fmmod_client = fmmod_client;

	ret = lpf_filter_init(&aflt->audio_lpf_l, cutoff_freq,
			      sample_rate, max_samples,
			      AUDIO_LPF_OVERLAP_FACTOR);
	if(ret < 0)
		return ret;

	ret = lpf_filter_init(&aflt->audio_lpf_r, cutoff_freq,
			      sample_rate, max_samples,
			      AUDIO_LPF_OVERLAP_FACTOR);
	if(ret < 0)
		return ret;

	ret = fmpreemph_filter_init(&aflt->fmprf_l, (float) sample_rate,
			      (float) cutoff_freq);
	if(ret < 0)
		return ret;

	ret = fmpreemph_filter_init(&aflt->fmprf_r, (float) sample_rate,
			      (float) cutoff_freq);
	if(ret < 0)
		return ret;

	afltd_l->lpf = &aflt->audio_lpf_l;
	afltd_l->fmprf = &aflt->fmprf_l;
	afltd_l->active = &aflt->active;

	afltd_r->lpf = &aflt->audio_lpf_r;
	afltd_r->fmprf = &aflt->fmprf_r;
	afltd_r->active = &aflt->active;

	aflt->active = 1;

#ifdef JMPXRDS_MT
	pthread_mutex_init(&afltd_l->proc_mutex, NULL);
	pthread_cond_init(&afltd_l->proc_trigger, NULL);
	pthread_mutex_init(&afltd_l->done_mutex, NULL);
	pthread_cond_init(&afltd_l->done_trigger, NULL);

	rtprio = jack_client_max_real_time_priority(aflt->fmmod_client);
	if(rtprio < 0)
		return ret;

	ret = jack_client_create_thread(aflt->fmmod_client, &afltd_l->tid,
					rtprio, 1,
					lpf_loop, (void *) afltd_l);
	if(ret < 0)
		return ret;
#endif

	return ret;
}

int
audio_filter_apply(struct audio_filter *aflt, float *samples_in_l,
		   float *samples_out_l, float *samples_in_r,
		   float *samples_out_r, uint16_t num_samples,
		   float gain_multiplier, uint8_t use_lp_filter,
		   enum lpf_preemph_mode preemph_tau_mode,
		   float* peak_l, float* peak_r)
{
	struct aflt_thread_data *afltd_l = &aflt->afltd_l;
	struct aflt_thread_data *afltd_r = &aflt->afltd_r;
	int ret = 0;
	int i = 0;

	/* If Low Pass filter is enabled, apply it afterwards, else
	 * just multiply with the gain multiplier and return */
	if(!use_lp_filter) {
		for(i = 0; i < num_samples; i++) {
			samples_out_l[i] = samples_in_l[i] * gain_multiplier;
			samples_out_r[i] = samples_in_r[i] * gain_multiplier;
		}
		return 0;
	}

#ifdef JMPXRDS_MT
	pthread_mutex_lock(&afltd_l->proc_mutex);
	afltd_l->in = samples_in_l;
	afltd_l->out = samples_out_l;
	afltd_l->gain = gain_multiplier;
	afltd_l->num_samples = num_samples;
	afltd_l->preemph_tau_mode = preemph_tau_mode;
	pthread_mutex_unlock(&afltd_l->proc_mutex);

	afltd_r->in = samples_in_r;
	afltd_r->out = samples_out_r;
	afltd_r->gain = gain_multiplier;
	afltd_r->num_samples = num_samples;
	afltd_r->preemph_tau_mode = preemph_tau_mode;

	/* Signal the left channel thread to start
	 * processing this chunk */
	pthread_mutex_lock(&afltd_l->proc_mutex);
	pthread_cond_signal(&afltd_l->proc_trigger);
	pthread_mutex_unlock(&afltd_l->proc_mutex);

	/* Process right channel on current thread */
	audio_filter_thread_run(afltd_r);

	/* Wait for the left channel thread to finish */
	while (pthread_cond_wait(&afltd_l->done_trigger, &afltd_l->done_mutex) != 0);

#else
	afltd_l->in = samples_in_l;
	afltd_l->out = samples_out_l;
	afltd_l->gain = gain_multiplier;
	afltd_l->num_samples = num_samples;
	afltd_l->preemph_tau_mode = preemph_tau_mode;

	afltd_r->in = samples_in_r;
	afltd_r->out = samples_out_r;
	afltd_r->gain = gain_multiplier;
	afltd_r->num_samples = num_samples;
	afltd_r->preemph_tau_mode = preemph_tau_mode;


	audio_filter_thread_run(afltd_l);
	audio_filter_thread_run(afltd_r);
#endif

	if (afltd_l->result || afltd_r->result)
		return -1;

	(*peak_l) = afltd_l->peak_gain;
	(*peak_r) = afltd_r->peak_gain;

	return 0;
}


/***********************************************\
* HILBERT TRANSFORMER FOR THE HARTLEY MODULATOR *
\***********************************************/

/*
 * The Hilbert transformer is an all-pass filter that shifts
 * the phase of the input signal by -pi/2 (90deg). To understand
 * this implementation think of the signal as a vector on the I/Q
 * plane. To rotate the vector by -pi/2 we need to swap Q with I.
 * We need to do that for both positive and negative frequencies
 * (it's not like the LP filter, we need the whole thing). This is
 * equivalent to multiplying positive frequencies with i
 * (so it's (0 +i) * (Re -iIm) = -Im +iRe) and negative frequencies
 * with -i (so it's (0 -i) * (Re +iIm) = Im -iRe). Note that if
 * we wanted +pi/2 we would do the oposite (-i for positive freqs
 * and i for negative).
 *
 * For more information about Hilbert transformation check out
 * http://www.katjaas.nl/hilbert/hilbert.html
 */

void
hilbert_transformer_destroy(struct hilbert_transformer_data *ht)
{
	if(ht->real_buff)
		fftwf_free(ht->real_buff);
	if(ht->complex_buff)
		fftwf_free(ht->complex_buff);
	if(ht->dft_plan)
		fftwf_destroy_plan(ht->dft_plan);
	if(ht->ift_plan)
		fftwf_destroy_plan(ht->ift_plan);
}

int
hilbert_transformer_init(struct hilbert_transformer_data *ht, uint16_t num_bins)
{
	int i = 0;
	int ret = 0;
	ht->num_bins = num_bins;

	/* Allocate buffers */
	ht->real_buff = fftwf_alloc_real(num_bins);
	if(!ht->real_buff) {
		ret = -1;
		goto cleanup;
	}
	memset(ht->real_buff, 0, num_bins * sizeof(float));


	/* Note: Instead of allocating bins / 2 + 1 as we did with
	 * the FIR filter, we allocate the full thing to get the mirroring
	 * effect. */
	ht->complex_buff = fftwf_alloc_complex(num_bins);
	if(!ht->complex_buff) {
		ret = -2;
		goto cleanup;
	}
	memset(ht->complex_buff, 0, num_bins * sizeof(fftwf_complex));


	/* Create DFT plan */
	ht->dft_plan = fftwf_plan_dft_r2c_1d(num_bins, ht->real_buff,
					     ht->complex_buff, FFTW_MEASURE);
	if(!ht->dft_plan) {
		ret = -3;
		goto cleanup;
	}


	/* Create IFT plan */
	ht->ift_plan = fftwf_plan_dft_c2r_1d(num_bins, ht->complex_buff,
					    ht->real_buff, FFTW_MEASURE);
	if(!ht->ift_plan)
		ret = -4;

 cleanup:
	if(ret < 0)
		hilbert_transformer_destroy(ht);
	return ret;
}

int
hilbert_transformer_apply(struct hilbert_transformer_data *ht, float *in,
			  uint16_t num_samples)
{
	float ratio = 0.0;
	float tmp = 0.0L;
	int middle_point = 0;
	int i = 0;

	/* Clear and fill the real buffer */
	memset(ht->real_buff, 0, ht->num_bins * sizeof(float));
	memcpy(ht->real_buff, in, num_samples * sizeof(float));

	/* Run the DFT plan to transform signal */
	fftwf_execute(ht->dft_plan);

	/* Now signal is on the complex buffer. */

	/* Get the first half (+1 here is to cover odd bins) */
	middle_point = (ht->num_bins + 1) / 2;

	/* -Im +iRe */
	for(i = 0; i < middle_point; i++) {
		ht->complex_buff[i][1] *= -1.0L;
		tmp = ht->complex_buff[i][1];
		ht->complex_buff[i][1] = ht->complex_buff[i][0];
		ht->complex_buff[i][0] = tmp;
	}

	/* Middle point */
	ht->complex_buff[i][0] = 0.0L;
	ht->complex_buff[i][1] = 0.0L;

	/* Im -iRe */
	for(i = middle_point + 1; i < ht->num_bins; i++) {
		ht->complex_buff[i][0] *= -1.0L;
		tmp = ht->complex_buff[i][1];
		ht->complex_buff[i][1] = ht->complex_buff[i][0];
		ht->complex_buff[i][0] = tmp;
	}

	/* Switch the signal back to the time domain */
	fftwf_execute(ht->ift_plan);

	/* Note that FFTW returns unnormalized data so the IFT output
	 * is multiplied with the product of the logical dimentions
	 * which in our case is num_bins.*/
	ratio = 1.0 / (float) ht->num_bins;

	for(i = 0; i < num_samples; i++)
		ht->real_buff[i] *= ratio;

	return 0;
}
