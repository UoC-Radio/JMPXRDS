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

/*
 * Sinc = sin(pi* x)/(pi * x)
 * https://en.wikipedia.org/wiki/Sinc_function
 */
inline static double
sinc(double phase)
{
	return (sin(M_PI * phase) / (M_PI * phase));
}

/*
 * Nutall window
 * https://en.wikipedia.org/wiki/Window_function
 */
inline static double
nutall_window(uint16_t bin, uint16_t num_bins)
{
	double width = (double) num_bins - 1.0L;

	double a0 = 0.355768L;
	double a1 = 0.487396L;
	double a2 = 0.144232L;
	double a3 = 0.012604L;

	return (a0 - a1 * cos((2.0L * M_PI * (double) bin) / width) +
		a2 * cos((4.0L * M_PI * (double) bin) / width) -
		a3 * cos((6.0L * M_PI * (double) bin) / width));
}

static void
generate_lpf_impulse(float* out, uint16_t num_bins,
			float cutoff_freq, float sample_rate)
{
	double fc_pre_warped = 2.0L * ((double) cutoff_freq / (double) sample_rate);
	double middle_bin = ((double) num_bins - 1.0L) / 2.0L;
	double phase = 0;
	int i = 0;

	/*
	 * Sinc impulse: h[n] = sinc(2*fc * (n - (N-1/2))
	 */
	for(i = 0; i < num_bins; i++) {
		phase = ((double) i - middle_bin) * fc_pre_warped;
		out[i] = (float) (0.2L * sinc(phase) * nutall_window(i, num_bins));
	}
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

	/* Corner angular cutoff frequency relative to the sampling rate */
	double pre_warped_wc = tan(cutoff_w_low /  (2.0L * (double) sample_rate));

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

int
fmpreemph_filter_init(struct fmpreemph_filter_data *fmprf,
		      float sample_rate)
{
	int ret = 0;

	ret = fmpreemph_filter_init_mode(fmprf,
			   sample_rate,
			   (float) AFLT_CUTOFF_FREQ,
			   50);
	if (ret < 0)
		return ret;

	ret = fmpreemph_filter_init_mode(fmprf,
			   sample_rate,
			   (float) AFLT_CUTOFF_FREQ,
			   75);

	return ret;
}

float
fmpreemph_filter_apply(struct fmpreemph_filter_data *fmprf,
		       float sample, enum fmpreemph_mode tau_mode)
{
	float out = 0.0;
	const float *ataps = NULL;
	const float *btaps = NULL;
	static enum fmpreemph_mode prev_tau_mode = LPF_PREEMPH_NONE;

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
	}

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

	return out * 8.0;
}

/*****************************\
* GENERIC FFT LOW-PASS FILTER *
\*****************************/

void
lpf_filter_destroy(struct lpf_filter_data *lpf)
{
	if(lpf->filter_resp)
		fftwf_free(lpf->filter_resp);
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
	float nyquist_freq = 0.0;
	int ret = 0;

	/* Initialize filter parameters */
	lpf->period_size = max_frames;
	lpf->num_bins = (overlap_factor + 1) * lpf->period_size;
	lpf->sample_rate = sample_rate;
	lpf->middle_bin = (lpf->num_bins / 2) + 1;
	nyquist_freq = (float) lpf->sample_rate / 2.0;
	lpf->bin_bw = (nyquist_freq / (float) lpf->num_bins);
	lpf->overlap_len = overlap_factor * lpf->period_size;


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


	/* Allocate buffer for the filter's responce */
	lpf->filter_resp = fftwf_alloc_complex(lpf->middle_bin - 0);
	if(!lpf->filter_resp) {
		ret = -4;
		goto cleanup;
	}


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


	/* Generate filter's responce on time domain on lpf->real_in and
	 * calculate its responce on the frequency domain */
	generate_lpf_impulse(lpf->real_in, lpf->num_bins,
			     (float) cutoff_freq, (float) sample_rate);

	fftwf_execute(lpf->dft_plan);

	/* Store the result on lpf->filter_resp and clear the complex
	 * buffer */
	memcpy(lpf->filter_resp, lpf->complex_buff,
	       sizeof(fftwf_complex) * (lpf->middle_bin - 0));
	memset(lpf->complex_buff, 0, sizeof(fftwf_complex) *
					(lpf->middle_bin - 0));


 cleanup:
	if(ret < 0)
		lpf_filter_destroy(lpf);
	return ret;
}

int
lpf_filter_apply(struct lpf_filter_data *lpf, const float *in, float *out,
		 uint16_t num_samples, float gain)
{
	float ratio = 0.0;
	fftw_complex tmp = {0};
	int i = 0;

	/* Shift the buffer's content to make room for the new
	 * period on its end and then put the new data there. */
	memmove(lpf->real_in, lpf->real_in + lpf->period_size,
		lpf->overlap_len * sizeof(float));
	memcpy(lpf->real_in + lpf->overlap_len, in,
		num_samples * sizeof(float));

	/* Run the DFT plan to get the freq domain (complex or
	 * analytical) representation of the signal */
	fftwf_execute(lpf->dft_plan);

	/* Now signal is on the complex buffer, convolution of 1d
	 * signals on the time domain equals piecewise multiplication
	 * on the frequency domain, so we multiply the complex
	 * representation of the signal with the complex representation
	 * of the filter's impulse. */
	for(i = 0; i < lpf->middle_bin; i++) {
		/* Real part */
		tmp[0] = lpf->filter_resp[i][0] * lpf->complex_buff[i][0] -
			 lpf->filter_resp[i][1] * lpf->complex_buff[i][1];
		/* Imaginary part */
		tmp[1] = lpf->filter_resp[i][0] * lpf->complex_buff[i][1] +
			 lpf->filter_resp[i][1] * lpf->complex_buff[i][0];

		lpf->complex_buff[i][0] = tmp[0];
		lpf->complex_buff[i][1] = tmp[1];
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
hilbert_transformer_apply(struct hilbert_transformer_data *ht, const float *in,
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
