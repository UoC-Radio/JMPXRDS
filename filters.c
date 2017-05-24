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
gaussian(uint16_t bin, double variance)
{
	return exp(-1.0L * pow((double) bin, 2) / (2.0L * variance));
}

static double
bin2freq(struct lpf_filter_data *lpf, uint16_t bin)
{
	double bratio = 0;
	double freq = 0;
	double nyquist_freq = 0;
	nyquist_freq = (double) lpf->sample_rate / 2.0L;
	bratio = (double) bin / (double) lpf->middle_bin;
	freq = bratio * nyquist_freq;
	return freq;
}

static uint16_t
freq2bin(struct lpf_filter_data *lpf, double freq)
{
	double fratio = 0;
	double nyquist_freq = 0;
	uint16_t bin = 0;
	nyquist_freq = (double) lpf->sample_rate / 2.0L;
	fratio = freq / nyquist_freq;
	bin = (uint16_t) (fratio * (double) lpf->middle_bin);
	return bin;
}


/********************************************************\
* GENERIC FFT LOW-PASS FILTER WITH OPTIONAL PRE-EMPHASIS *
\********************************************************/

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
	double trans_bw = 0.0L;
	uint16_t remaining_bins = 0;
	double nyquist_freq = 0.0L;

	lpf->num_bins = max_frames;
	lpf->sample_rate = sample_rate;
	lpf->middle_bin = (lpf->num_bins / 2) + 1;
	nyquist_freq = (double) lpf->sample_rate / 2.0L;
	lpf->bin_bw = (nyquist_freq / (double) lpf->num_bins);

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
	lpf->cutoff_bin = freq2bin(lpf, cutoff_freq);

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
	trans_bw = 2500.0L / lpf->bin_bw; /* 2.5KHz should be enough */
	lpf->variance = 2.0L * trans_bw;

	/* Calculate and store the filter's FFT curve from cutoff_bin
	 * to the end of the spectrum */
	remaining_bins = lpf->middle_bin - lpf->cutoff_bin;
	lpf->filter_curve = (double*) malloc(remaining_bins * sizeof(double));
	if(!lpf->filter_curve) {
		ret = -1;
		goto cleanup;
	}

	for(i = 0; i < remaining_bins; i++)
		lpf->filter_curve[i] = gaussian(i, lpf->variance);


	/* Allocate buffers for DFT/IFT */
	lpf->real_buff = fftw_alloc_real(max_frames);
	if(!lpf->real_buff) {
		ret = -2;
		goto cleanup;
	}
	memset(lpf->real_buff, 0, sizeof(double) * max_frames);

	lpf->complex_buff = fftw_alloc_complex(lpf->middle_bin - 0);
	if(!lpf->complex_buff) {
		ret = -3;
		goto cleanup;
	}
	memset(lpf->complex_buff, 0, sizeof(fftw_complex) *
					(lpf->middle_bin - 0));


	/* Create DFT plan */
	lpf->dft_plan = fftw_plan_dft_r2c_1d(lpf->num_bins, lpf->real_buff,
					     lpf->complex_buff, FFTW_MEASURE);
	if(!lpf->dft_plan) {
		ret = -4;
		goto cleanup;
	}


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
		 uint16_t num_samples, float gain, uint8_t preemph_tau)
{
	double tau = 0.0L;
	float ratio = 0.0;
	double fc = 0.0;
	uint16_t preemph_start_bin = 0;
	double pe_resp = 0.0L;
	double bin_bw_scaled = 0.0L;
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

	/* Now signal is on the complex buffer, apply pre-emphasis
	 * if requested and / or filter-out all frequency bins above
	 * the cutoff_bin */

	/* The FM Pre-emphasis filter is an RC high-pass filter with
	 * a single pole depending on the region. After the pole, the
	 * filter's Bode diagram gives a 20dB/decade which means that
	 * when the frequency is multiplied by 10 the gain is increased
	 * by 20dB. If we make this mapping between amplitude and frequency
	 * we'll get (1, 10^(0/20)), (10, 10^(20/20)), (100, 10^(40/20)),
	 * (1000, 10^(60/20))... This becomes (1,1), (10,10), (100, 100),
	 * (1000, 1000)... which is a straight line, so the response is a
	 * linear function of the frequency. */

	/* Different regions have different tau, which translates to
	 * a different cutoff frequency for the filter */
	switch(preemph_tau) {
		/* t = R*C = 1 / 2*pi*fc */
		/* fc = 1 / 2*pi*t */
		case LPF_PREEMPH_50US:
			tau = 0.000001 * (double)50;
			bin_bw_scaled = lpf->bin_bw * 0.00076L;
			break;
		case LPF_PREEMPH_75US:
			tau = 0.000001 * (double)75;
			bin_bw_scaled = lpf->bin_bw * 0.00115L;
			break;
		case LPF_PREEMPH_NONE:
		default:
			goto skip_preemph;
	};
	fc = 1.0 / (2.0 * M_PI * tau);
	preemph_start_bin = freq2bin(lpf, fc);

	/* Since we want to make this a pre-emphasis filter instead of a
	 * high-pass filter, we add + 1 below so it starts from the original
	 * gain. Also since we have bins on x axis we need to multiply by
	 * bin_bw. The scale factor (slope) above is something I came up with
	 * durring testing, to be within spec. Note that we don't mess with the
	 * imaginary part here since it encodes the phase and we don't want
	 * to introduce any distortion there. */
	for(i = preemph_start_bin, c = 0; i < lpf->cutoff_bin; i++, c++) {
		pe_resp = 1 + (double) c * bin_bw_scaled;
		lpf->complex_buff[i][0] *= pe_resp;
	}

	/* Continue as a high-shelf filter, no need to keep on
	 * increasing the amplitude */
	for(; i < lpf->middle_bin; i++)
		lpf->complex_buff[i][0] *= pe_resp;

 skip_preemph:

	/* Multiply the input signal -after the cutoff bin- with the filter's
	 * response. Again there is no need to play with the imaginary part
	 * in the stopband, we can just zero it out instead of doing extra
	 * multiplications */
	for(i = lpf->cutoff_bin, c = 0; i < lpf->middle_bin; i++, c++) {
		lpf->complex_buff[i][0] *= lpf->filter_curve[c];
		lpf->complex_buff[i][1] = 0.0L;
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


/***********************************************\
* COMBINED AUDIO FILTER (FM PRE-EMPHASIS + LPF) *
\***********************************************/

void
audio_filter_destroy(struct audio_filter *aflt)
{
	lpf_filter_destroy(&aflt->audio_lpf_l);
	lpf_filter_destroy(&aflt->audio_lpf_r);
}

int
audio_filter_init(struct audio_filter *aflt, uint32_t cutoff_freq,
		  uint32_t sample_rate, uint16_t max_samples)
{
	int ret = 0;

	ret = lpf_filter_init(&aflt->audio_lpf_l, cutoff_freq,
			      sample_rate, max_samples);
	if(ret < 0)
		return ret;

	ret = lpf_filter_init(&aflt->audio_lpf_r, cutoff_freq,
			      sample_rate, max_samples);
	return ret;
}

int
audio_filter_apply(struct audio_filter *aflt, float *samples_in_l,
		   float *samples_out_l, float *samples_in_r,
		   float *samples_out_r, uint16_t num_samples,
		   float gain_multiplier, uint8_t use_lp_filter,
		   uint8_t preemph_tau)
{
	struct lpf_filter_data *lpf_l = &aflt->audio_lpf_l;
	struct lpf_filter_data *lpf_r = &aflt->audio_lpf_r;
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

	/* Frequency domain convolution */
	lpf_filter_apply(lpf_l, samples_in_l, samples_out_l,
			     num_samples, gain_multiplier, preemph_tau);
	lpf_filter_apply(lpf_r, samples_in_r, samples_out_r,
			     num_samples, gain_multiplier, preemph_tau);

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
		ret = -3;
		goto cleanup;
	}


	/* Create IFT plan */
	ht->ift_plan = fftw_plan_dft_c2r_1d(num_bins, ht->complex_buff,
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
