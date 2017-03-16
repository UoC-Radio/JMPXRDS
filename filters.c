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

/*********\
* HELPERS *
\*********/

/* Note that we don't use the normalization factor
 * here so that it always starts at 1.0 -exp(0)-. */
static double
gaussian(uint16_t bin, uint16_t variance)
{
	double c = (double) variance;
	/* Cutoff bin is at 0 */
	uint32_t tmp = bin;
	tmp *= tmp;
	return exp(-(double)tmp / c);
}

/*****************************\
* GENERIC FFT LOW-PASS FILTER *
\*****************************/

void
lpf_filter_destroy(struct lpf_filter_data *lpf)
{
	if(lpf->filter_curve)
		free(lpf->filter_curve);
	if(lpf->real_buff)
		fftw_free(lpf->real_buff);
	if(lpf->complex_buff)
		fftw_free(lpf->complex_buff);
	if(lpf->dft_plan)
		fftw_destroy_plan(lpf->dft_plan);
	if(lpf->ift_plan)
		fftw_destroy_plan(lpf->ift_plan);
}

int
lpf_filter_init(struct lpf_filter_data *lpf, uint32_t cutoff_freq,
		uint32_t sample_rate, uint16_t max_frames)
{
	int i = 0;
	int ret = 0;
	uint32_t nyquist_freq = 0;
	double passband_ratio = 0.0L;
	double trans_bw = 0.0L;
	double trans_ratio = 0.0L;
	uint16_t variance = 0;
	lpf->num_bins = max_frames;
	uint16_t half_bins = (lpf->num_bins / 2) + 1;
	uint16_t remaining_bins = 0;

	/*
	 * Calculate the frequency bin after which we 'll start
	 * filtering out the remaining bins. The frequency bins will cover
	 * frequencies from 0 to Nyquist frequency so we have
	 * to calculate what portion of that spectrum is our passband
	 * (below the cutoff frequency), relative to the Nyquist
	 * frequency.
	 *
	 * Note: Because the signal is purely real, the FFT spectrum will
	 * be "mirrored" -conjugate symmetric-, so we only need to
	 * use half of it (positive frequencies) plus the point in the middle).
	 * That's why we have half_bins here and below.
	 */
	nyquist_freq = sample_rate / 2.0;
	passband_ratio = (double) cutoff_freq / (double) nyquist_freq;
	lpf->cutoff_bin = (uint16_t) (passband_ratio * (double) (half_bins));

	/*
	 * We don't want the filter to bee too steep (like a sinc filter which
	 * zeroes out everything after the cutoff bin) to avoid the "ringing"
	 * noise. Instead use a Gaussian curve (the second half part of the
	 * "bell") to make the transition smooth.
	 *
	 * We calculate the Gaussian's variance -the witdh of the bell- for
	 * a given transition bandwidth, in bins, the same way we did it above
	 * for the cutoff frequency bin.
	 */
	trans_bw = 2500.0L; /* 2.5KHz should be enough */
	trans_ratio = trans_bw / (double) nyquist_freq;
	variance = 2 * (uint16_t) (trans_ratio * (double) (half_bins));

	/* Calculate and store the filter's FFT curve from cutoff_bin
	 * to the end of the spectrum */
	remaining_bins = half_bins - lpf->cutoff_bin;
	lpf->filter_curve = malloc(remaining_bins * sizeof(double));
	if(!lpf->filter_curve) {
		ret = -1;
		goto cleanup;
	}

	for(i = 0; i < remaining_bins; i++)
		lpf->filter_curve[i] = gaussian(i, variance);


	/* Allocate buffers for DFT/IFT */
	lpf->real_buff = fftw_alloc_real(max_frames);
	if(!lpf->real_buff) {
		ret = -2;
		goto cleanup;
	}
	memset(lpf->real_buff, 0, sizeof(double) * max_frames);

	lpf->complex_buff = fftw_alloc_complex(half_bins);
	if(!lpf->complex_buff) {
		ret = -3;
		goto cleanup;
	}
	memset(lpf->complex_buff, 0, sizeof(fftw_complex) * half_bins);


	/* Create DFT plan */
	lpf->dft_plan = fftw_plan_dft_r2c_1d(lpf->num_bins, lpf->real_buff,
					     lpf->complex_buff, FFTW_MEASURE);
	if(!lpf->dft_plan) {
		ret = -4;
		goto cleanup;
	}
	fftw_execute(lpf->dft_plan);


	/* Create IFT plan */
	lpf->ift_plan = fftw_plan_dft_c2r_1d(lpf->num_bins, lpf->complex_buff,
					     lpf->real_buff, FFTW_MEASURE);
	if(!lpf->ift_plan)
		ret = -5;

 cleanup:
	if(ret < 0)
		lpf_filter_destroy(lpf);
	return ret;
}

int
lpf_filter_apply(struct lpf_filter_data *lpf, float *in, float *out,
		 uint16_t num_samples, float gain)
{
	float ratio = 0.0;
	int middle_bin = lpf->num_bins / 2 + 1;
	int i = 0;
	int c = 0;

	/* Copy samples to the real buffer, converting them to
	 * double on the way */
	memset(lpf->real_buff, 0, sizeof(double) * num_samples);
	for(i = 0; i < num_samples; i++)
		lpf->real_buff[i] = (double) in[i];

	/* Run the DFT plan to get the freq domain (complex or
	 * analytical) representation of the signal */
	fftw_execute(lpf->dft_plan);

	/* Now signal is on the complex buffer, filter-out
	 * all frequency bins above the cutoff_bin */
	for(i = lpf->cutoff_bin; i < middle_bin; i++, c++) {
		lpf->complex_buff[i][0] *= lpf->filter_curve[c];
		lpf->complex_buff[i][1] = 0;
	}

	/* Switch the signal back to the time domain */
	fftw_execute(lpf->ift_plan);

	/* Note that FFTW returns unnormalized data so the IFT output
	 * is multiplied with the product of the logical dimentions
	 * which in our case is bins.
	 * To make things simpler and more efficient, we calculate a gain
	 * ratio that will handle both the requested gain and the
	 * normalization (multiplication is cheaper than division). */
	ratio = (float) gain / (float) lpf->num_bins;

	for(i = 0; i < num_samples; i++)
		out[i] = ((float) lpf->real_buff[i]) * ratio;

	return 0;
}


/******************************\
* FM PRE-EMPHASIS AUDIO FILTER *
\******************************/

/*
 * This is a high-shelf biquad IIR filter that boosts frequencies
 * above its cutoff frequency by 20db/octave. The cutoff
 * frequency is calculated from the time constant of the analog
 * RC filter and is different on US (75us) and EU (50us)
 * radios.
 */

static int
fmpreemph_filter_init(struct fmpreemph_filter_data *iir,
		      uint32_t sample_rate,
		      uint8_t preemph_tau_usecs)
{
	double tau = 0.0;
	double fc = 0.0;
	double cutoff_freq = 0.0;
	double pre_warped_fc = 0.0;
	double re = 0.0;
	double im = 0.0;
	double gain = 0.0;
	double slope = 0.0;
	double A = 0.0;
	double alpha = 0.0;
	double a[3] = { 0 };
	double b[3] = { 0 };

	tau = 0.000001 * (double)preemph_tau_usecs;

	/* t = R*C = 1 / 2*pi*fc */
	/* fc = 1 / 2*pi*t */
	cutoff_freq = 1.0 / (2.0 * M_PI * tau);
	fc = cutoff_freq / (double)sample_rate;

	pre_warped_fc = 2.0 * M_PI * fc;
	re = cos(pre_warped_fc);
	im = sin(pre_warped_fc);

	/* This implementation comes from Audio EQ Coockbook
	 * from Robert Bristow-Johnson. You can read it online
	 * at http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
	 * The gain and slope values come from SoX's CD de emphasis
	 * filter (the pre empasis is the same as in FM at 50us) */
	gain = 9.477;
	slope = 0.4845;

	/* Work in log scale to avoid calculating 10 ^ (gain/40) */
	A = exp(gain / 40.0 * log(10.0));

	alpha = im / 2 * sqrt((A + 1 / A) * (1 / slope - 1) + 2);

	b[0] = A * ((A + 1) + (A - 1) * re + 2 * sqrt(A) * alpha);
	b[1] = -2 * A * ((A - 1) + (A + 1) * re);
	b[2] = A * ((A + 1) + (A - 1) * re - 2 * sqrt(A) * alpha);
	a[0] = (A + 1) - (A - 1) * re + 2 * sqrt(A) * alpha;
	a[1] = 2 * ((A - 1) - (A + 1) * re);
	a[2] = (A + 1) - (A - 1) * re - 2 * sqrt(A) * alpha;

	iir->iir_ataps[0] = (float)(b[0] / a[0]);
	iir->iir_ataps[1] = (float)(b[1] / a[0]);
	iir->iir_ataps[2] = (float)(b[2] / a[0]);
	iir->iir_btaps[0] = (float)(a[1] / a[0]);
	iir->iir_btaps[1] = (float)(a[2] / a[0]);

	return 0;
}

/*
 * Apply the IIR FM Pre-emphasis filter to increase the
 * gain as the frequency gets higher and compensate for
 * the increased noise on higher frequencies. A de-emphasis
 * filter is used on the receiver to undo that.
 */
static float
fmpreemph_filter_apply(struct fmpreemph_filter_data *iir,
		       float sample, uint8_t chan_idx)
{
	float out = 0.0;
	float *iir_inbuf = NULL;
	float *iir_outbuf = NULL;

	if (chan_idx == 0) {
		iir_inbuf = iir->iir_inbuff_l;
		iir_outbuf = iir->iir_outbuff_l;
	} else if (chan_idx == 1) {
		iir_inbuf = iir->iir_inbuff_r;
		iir_outbuf = iir->iir_outbuff_r;
	} else
		return 0.0;

	/* y = a0*in + a1*(in-1) + a2*(in-2) - b0*(out-1) - b1*(out -2) */
	out = iir->iir_ataps[0] * sample +
	    iir->iir_ataps[1] * iir_inbuf[1] +
	    iir->iir_ataps[2] * iir_inbuf[0] -
	    iir->iir_btaps[0] * iir_outbuf[1] -
	    iir->iir_btaps[1] * iir_outbuf[0];

	/* update in-2 and in-1 */
	iir_inbuf[0] = iir_inbuf[1];
	iir_inbuf[1] = sample;

	/* update out-1 and out-2 */
	iir_outbuf[0] = iir_outbuf[1];
	iir_outbuf[1] = out;

	return out;
}


/***********************************************\
* COMBINED AUDIO FILTER (FM PRE-EMPHASIS + LPF) *
\***********************************************/

void
audio_filter_destroy(struct audio_filter *aflt)
{
	lpf_filter_destroy(&aflt->audio_lpf);
}

int
audio_filter_init(struct audio_filter *aflt, uint32_t cutoff_freq,
		  uint32_t sample_rate, uint16_t max_samples,
		  uint8_t preemph_tau_usecs)
{
	int ret = 0;

	ret = lpf_filter_init(&aflt->audio_lpf, cutoff_freq,
			      sample_rate, max_samples);
	if (ret < 0)
		return ret;

	fmpreemph_filter_init(&aflt->fm_preemph, sample_rate,
			      preemph_tau_usecs);
	return 0;
}

int
audio_filter_apply(struct audio_filter *aflt, float *samples_in_l,
		   float *samples_out_l, float *samples_in_r,
		   float *samples_out_r, uint16_t num_samples,
		   float gain_multiplier, uint8_t use_lp_filter)
{
	struct lpf_filter_data *lpf = &aflt->audio_lpf;
	int i = 0;

	/* First apply the FM pre-emphasis IIR filter */
	for(i = 0; i < num_samples; i++) {
		samples_out_l[i] = fmpreemph_filter_apply(&aflt->fm_preemph,
							samples_in_l[i], 0);
		samples_out_r[i] = fmpreemph_filter_apply(&aflt->fm_preemph,
							samples_in_r[i], 1);
	}

	/* If Low Pass filter is enabled, apply it afterwards, else
	 * just multiply with the gain multiplier and return */
	if(!use_lp_filter) {
		for(i = 0; i < num_samples; i++) {
			samples_out_l[i] *= gain_multiplier;
			samples_out_r[i] *= gain_multiplier;
		}
		return 0;
	}

	/* Frequency domain convolution */
	lpf_filter_apply(lpf, samples_out_l, samples_out_l,
			     num_samples, gain_multiplier);
	lpf_filter_apply(lpf, samples_out_r, samples_out_r,
			     num_samples, gain_multiplier);

	return 0;
}


/****************************************************\
* BUTTERWORTH IIR LP FILTER FOR THE WEAVER MODULATOR *
\****************************************************/

/*
 * This is a filter with fixed coefficients to cut
 * anything above half the Niquist frequency. It's used
 * as the low pass filter for the Weaver SSB modulator.
 */

/* Coefficients and the WEAVER_FILTER_REVERSE_MAX_GAIN
 * macro were calculated using
 * http://www-users.cs.york.ac.uk/~fisher/mkfilter/ */
int
iir_ssb_filter_init(struct ssb_filter_data *iir)
{

	iir->iir_ataps[0] = 1;
	iir->iir_ataps[1] = 10;
	iir->iir_ataps[2] = 45;
	iir->iir_ataps[3] = 120;
	iir->iir_ataps[4] = 210;
	iir->iir_ataps[5] = 252;

	iir->iir_btaps[0] = -0.0000223708;
	iir->iir_btaps[1] = 0.0002921703;
	iir->iir_btaps[2] = -0.0040647116;
	iir->iir_btaps[3] = 0.0147536451;
	iir->iir_btaps[4] = -0.0945583553;
	iir->iir_btaps[5] = 0.1621107260;
	iir->iir_btaps[6] = -0.6336867140;
	iir->iir_btaps[7] = 0.5477895114;
	iir->iir_btaps[8] = -1.4564581781;
	iir->iir_btaps[9] = 0.5241910939;

	return 0;
}

float
iir_ssb_filter_apply(struct ssb_filter_data *iir,
		     float sample, uint8_t chan_idx)
{
	float *iir_inbuf = NULL;
	float *iir_outbuf = NULL;
	float out_sample = 0;
	int i = 0;

	if (chan_idx == 0) {
		iir_inbuf = iir->iir_inbuff_l;
		iir_outbuf = iir->iir_outbuff_l;
	} else if (chan_idx == 1) {
		iir_inbuf = iir->iir_inbuff_r;
		iir_outbuf = iir->iir_outbuff_r;
	} else
		return 0.0;

	/* Make room for the new in sample */
	for (i = 0; i < WEAVER_FILTER_SIZE - 1; i++)
		iir_inbuf[i] = iir_inbuf[i + 1];

	/* Put it on the filter's ring buffer */
	iir_inbuf[WEAVER_FILTER_SIZE - 1] =
	    sample * WEAVER_FILTER_REVERSE_MAX_GAIN;

	/* Process input ring buffer */
	for (i = 0; i < WEAVER_FILTER_TAPS / 2; i++)
		out_sample += iir->iir_ataps[i] *
			(iir_inbuf[i] + iir_inbuf[WEAVER_FILTER_TAPS - i]);

	out_sample +=
		iir->iir_ataps[WEAVER_FILTER_TAPS / 2] *
			iir_inbuf[WEAVER_FILTER_TAPS / 2];

	/* Make room for the new out sample */
	for (i = 0; i < WEAVER_FILTER_SIZE - 1; i++)
		iir_outbuf[i] = iir_outbuf[i + 1];

	/* Process output ring buffer */
	for (i = 0; i < WEAVER_FILTER_TAPS; i++)
		out_sample += iir->iir_btaps[i] * iir_outbuf[i];

	/* Put output sample on the output ring buffer */
	iir_outbuf[10] = out_sample;

	return out_sample;
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
		fftw_free(ht->real_buff);
	if(ht->complex_buff)
		fftw_free(ht->complex_buff);
	if(ht->dft_plan)
		fftw_destroy_plan(ht->dft_plan);
	if(ht->ift_plan)
		fftw_destroy_plan(ht->ift_plan);
}

int
hilbert_transformer_init(struct hilbert_transformer_data *ht, uint16_t num_bins)
{
	int i = 0;
	int ret = 0;
	ht->num_bins = num_bins;

	/* Allocate buffers */
	ht->real_buff = fftw_alloc_real(num_bins);
	if(!ht->real_buff) {
		ret = -1;
		goto cleanup;
	}
	memset(ht->real_buff, 0, sizeof(double) * num_bins);


	/* Note: Instead of allocating bins / 2 + 1 as we did with
	 * the FIR filter, we allocate the full thing to get the mirroring
	 * effect. */
	ht->complex_buff = fftw_alloc_complex(num_bins);
	if(!ht->complex_buff) {
		ret = -2;
		goto cleanup;
	}
	memset(ht->complex_buff, 0, sizeof(fftw_complex) * num_bins);


	/* Create DFT plan */
	ht->dft_plan = fftw_plan_dft_r2c_1d(num_bins, ht->real_buff,
					     ht->complex_buff, FFTW_MEASURE);
	if(!ht->dft_plan) {
		ret = -5;
		goto cleanup;
	}
	fftw_execute(ht->dft_plan);


	/* Create IFT plan */
	ht->ift_plan = fftw_plan_dft_c2r_1d(num_bins, ht->complex_buff,
					    ht->real_buff, FFTW_MEASURE);
	if(!ht->ift_plan)
		ret = -6;

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
	double tmp = 0.0L;
	int middle_point = 0;
	int i = 0;

	/* Copy samples to the real buffer, converting them to
	 * double on the way */
	memset(ht->real_buff, 0, sizeof(double) * ht->num_bins);
	for(i = 0; i < num_samples; i++)
		ht->real_buff[i] = (double) in[i];

	/* Run the DFT plan to transform signal */
	fftw_execute(ht->dft_plan);

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
	fftw_execute(ht->ift_plan);

	/* Note that FFTW returns unnormalized data so the IFT output
	 * is multiplied with the product of the logical dimentions
	 * which in our case is num_bins.*/
	ratio = (double) 1.0 / (double) ht->num_bins;

	for(i = 0; i < num_samples; i++)
		ht->real_buff[i] *= ratio;

	return 0;
}
