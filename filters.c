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
#include <stdlib.h>	/* For NULL */
#include <string.h>	/* For memset */
#include <math.h>	/* For sin, cos and M_PI */


/***************************************\
* GENERIC FIR LOW-PASS FILTER FOR AUDIO *
\***************************************/

/*
 * That's a typical Sinc filter, multiplied by a
 * Blackman - Harris window. For more information
 * Wikipedia is your friend. We use this to protect
 * the stereo pilot (19KHz) from audio and for the
 * FIR filter-based SSB modulator.
 */

/* Sinc = sin(pi* x)/(pi * x) */
static double inline
sinc(double phase)
{
	return (sin(M_PI * phase) / (M_PI * phase));
}

/* Sinc filter:
 * 	h[n] = sinc(2*fc * (n - (N-1/2))
 */
static double inline
sinc_filter(double fc_doubled, uint16_t bin)
{
	return sinc(fc_doubled * (double)(bin - FIR_FILTER_HALF_SIZE));
}

/* Blackman - Harris window */
static double inline
blackman_window(uint16_t bin)
{
	double a0, a1, a2, a3;

	a0 = 0.35875;
	a1 = 0.48829;
	a2 = 0.14128;
	a3 = 0.01168;

	return (a0 - a1 * cos((2 * M_PI * bin)/(FIR_FILTER_SIZE - 1)) +
		a2 * cos((4 * M_PI * bin)/(FIR_FILTER_SIZE -1)) -
		a3 * cos((6 * M_PI * bin)/(FIR_FILTER_SIZE -1)));
}

int
fir_filter_init(struct fir_filter_data *fir, uint32_t cutoff_freq,
						 uint32_t sample_rate)
{
	int i = 0;
	double fc_doubled = 0;
	double sum = 0;

	/* Fc: cutoff frequency as a fraction of sample rate */
	fc_doubled = 2.0 * ((double) cutoff_freq / (double) sample_rate);

	memset(fir, 0, sizeof(struct fir_filter_data));

	/* Since sinc filter (and the window too) is symmetric
	 * store half of it */
	for(i = 0; i < FIR_FILTER_HALF_SIZE; i++) {
		/* Windowed sinc filter */
		fir->fir_coeffs[i] = sinc_filter(fc_doubled, i) *
		blackman_window(i);
	}

	/* Normalize */
	for(i = 0; i < FIR_FILTER_HALF_SIZE; i++)
		sum += fir->fir_coeffs[i];

	sum *= 2;

	for(i = 0; i < FIR_FILTER_HALF_SIZE; i++)
		fir->fir_coeffs[i] /= sum;

	return 0;
}

float
fir_filter_apply(struct fir_filter_data *fir, float sample, uint8_t chan_idx)
{
	int i;
	float out = 0;
	float *fir_buf = NULL;
	int16_t previous, later;

	if(chan_idx == 0)
		fir_buf = fir->fir_buff_l;
	else if(chan_idx == 1)
		fir_buf = fir->fir_buff_r;
	else
		return 0;

	fir_buf[fir->fir_index] = sample;

	previous = fir->fir_index;
	later = fir->fir_index;

	/* Apply the FIR low-pass filter to cut frequencies
	 * above the cutoff frequency */
	for(i = 0; i < FIR_FILTER_HALF_SIZE; i++) {
		previous--;
		if(previous < 0)
			previous = FIR_FILTER_SIZE - 1;

		later++;
		if(later >= FIR_FILTER_SIZE)
			later = 0;

		out += fir->fir_coeffs[i] * (fir_buf[previous] +
						fir_buf[later]);
	}

	return out;
}

void
fir_filter_update(struct fir_filter_data *fir)
{
	fir->fir_index = (fir->fir_index + 1) % FIR_FILTER_SIZE;
}


/******************************\
* FM PRE-EMPHASIS AUDIO FILTER *
\******************************/

/*
 * This is a high-shelf biquad filter that boosts frequencies
 * above its cutoff frequency by 20db/octave. The cutoff
 * frequency is calculated from the time constant of the analog
 * RC filter and is different on US (75us) and EU (50us)
 * radios.
 */

static int
fmpreemph_filter_init(struct fmpreemph_filter_data *iir, uint32_t sample_rate,
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
	double a[3] = {0};
	double b[3] = {0};

	tau = 0.000001 * (double) preemph_tau_usecs;

	/* t = R*C = 1 / 2*pi*fc */
	/* fc = 1 / 2*pi*t */
	cutoff_freq = 1.0 / (2.0 * M_PI * tau);
	fc = cutoff_freq / (double) sample_rate;

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

	alpha = im/2 * sqrt((A + 1/A) * (1/slope - 1) + 2);

	b[0] =	A*( (A+1) + (A-1)*re + 2*sqrt(A)*alpha );
	b[1] = -2*A*( (A-1) + (A+1)*re				   );
	b[2] =	A*( (A+1) + (A-1)*re - 2*sqrt(A)*alpha );
	a[0] =		(A+1) - (A-1)*re + 2*sqrt(A)*alpha;
 	a[1] =	2*( (A-1) - (A+1)*re				   );
	a[2] =		(A+1) - (A-1)*re - 2*sqrt(A)*alpha;

	iir->iir_ataps[0] = (float) (b[0] / a[0]);
	iir->iir_ataps[1] = (float) (b[1] / a[0]);
	iir->iir_ataps[2] = (float) (b[2] / a[0]);
	iir->iir_btaps[0] = (float) (a[1] / a[0]);
	iir->iir_btaps[1] = (float) (a[2] / a[0]);

	return 0;
}

static float
fmpreemph_filter_apply(struct fmpreemph_filter_data *iir, float sample,
							uint8_t chan_idx)
{
	float out = 0.0;
	float *iir_inbuf = NULL;
	float *iir_outbuf = NULL;

	if(chan_idx == 0) {
		iir_inbuf = iir->iir_inbuff_l;
		iir_outbuf = iir->iir_outbuff_l;
	} else if(chan_idx == 1) {
		iir_inbuf = iir->iir_inbuff_r;
		iir_outbuf = iir->iir_outbuff_r;
	} else
		return 0.0;

	/* Apply the IIR FM Pre-emphasis filter to increase the
	 * gain as the frequency gets higher and compensate for
	 * the increased noise on higher frequencies. A de-emphasis
	 * filter is used on the receiver to undo that */

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
audio_filter_init(struct audio_filter *aflt, uint32_t cutoff_freq,
						uint32_t sample_rate,
						uint8_t preemph_tau_usecs)
{
	fir_filter_init(&aflt->audio_lpf, cutoff_freq, sample_rate);
	fmpreemph_filter_init(&aflt->fm_preemph, sample_rate, preemph_tau_usecs);
}

void
audio_filter_update(struct audio_filter *aflt)
{
	fir_filter_update(&aflt->audio_lpf);
}

float
audio_filter_apply(struct audio_filter *aflt, float sample, uint8_t chan_idx, uint8_t use_lp_filter)
{
	float out = 0;

	out = fmpreemph_filter_apply(&aflt->fm_preemph, sample, chan_idx);
	if(use_lp_filter)
		out = fir_filter_apply(&aflt->audio_lpf, out, chan_idx);

	return out;
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
iir_ssb_filter_apply(struct ssb_filter_data *iir, float sample,
						uint8_t chan_idx)
{
	float *iir_inbuf = NULL;
	float *iir_outbuf = NULL;
	float out_sample = 0;
	int i = 0;

	if(chan_idx == 0) {
		iir_inbuf = iir->iir_inbuff_l;
		iir_outbuf = iir->iir_outbuff_l;
	} else if(chan_idx == 1) {
		iir_inbuf = iir->iir_inbuff_r;
		iir_outbuf = iir->iir_outbuff_r;
	} else
		return 0.0;

	/* Make room for the new in sample */
	for(i = 0; i < WEAVER_FILTER_SIZE - 1; i++)
		iir_inbuf[i] = iir_inbuf[i + 1];

	/* Put it on the filter's ring buffer */
	iir_inbuf[WEAVER_FILTER_SIZE - 1] = sample * WEAVER_FILTER_REVERSE_MAX_GAIN;


	/* Process input ring buffer */
	for(i = 0; i < WEAVER_FILTER_TAPS / 2; i++)
		out_sample += iir->iir_ataps[i] * (iir_inbuf[i]
	+ iir_inbuf[WEAVER_FILTER_TAPS - i]);
	out_sample += iir->iir_ataps[WEAVER_FILTER_TAPS / 2] *
	iir_inbuf[WEAVER_FILTER_TAPS / 2];

	/* Make room for the new out sample */
	for(i = 0; i < WEAVER_FILTER_SIZE - 1; i++)
		iir_outbuf[i] = iir_outbuf[i + 1];

	/* Process output ring buffer */
	for(i = 0; i < WEAVER_FILTER_TAPS; i++)
		out_sample += iir->iir_btaps[i] * iir_outbuf[i];

	/* Put output sample on the output ring buffer */
	iir_outbuf[10] = out_sample;

	return out_sample;
}


/***********************************************\
* HILBERT TRANSFORMER FOR THE HARTLEY MODULATOR *
\***********************************************/


/* Coefficients and the SSB_FILTER_GAIN macro were calculated using
 * http://www-users.cs.york.ac.uk/~fisher/mkfilter/ */
static float ht_coeffs[] = 
	{0.0000000000, +0.0026520976, +0.0000000000, +0.0034416361,
	+0.0000000000, +0.0049746748, +0.0000000000, +0.0073766077,
	+0.0000000000, +0.0107903952, +0.0000000000, +0.0153884524,
	+0.0000000000, +0.0213931078, +0.0000000000, +0.0291124774,
	+0.0000000000, +0.0390058590, +0.0000000000, +0.0518100732,
	+0.0000000000, +0.0688038635, +0.0000000000, +0.0924245456,
	+0.0000000000, +0.1279406869, +0.0000000000, +0.1891367563,
	+0.0000000000, +0.3267308515, +0.0000000000, +0.9977849743,
	+0.0000000000, -0.9977849743, -0.0000000000, -0.3267308515,
	-0.0000000000, -0.1891367563, -0.0000000000, -0.1279406869,
	-0.0000000000, -0.0924245456, -0.0000000000, -0.0688038635,
	-0.0000000000, -0.0518100732, -0.0000000000, -0.0390058590,
	-0.0000000000, -0.0291124774, -0.0000000000, -0.0213931078,
	-0.0000000000, -0.0153884524, -0.0000000000, -0.0107903952,
	-0.0000000000, -0.0073766077, -0.0000000000, -0.0049746748,
	-0.0000000000, -0.0034416361, -0.0000000000, -0.0026520976,
	-0.0000000000};


int
hilbert_transformer_init(struct hilbert_transformer_data *ht)
{
	memcpy(ht->fir_coeffs, ht_coeffs, HT_FIR_FILTER_SIZE * sizeof(float));
	return 0;
}

float
hilbert_transformer_apply(struct hilbert_transformer_data *ht, float sample)
{
	int i = 0;
	float out = 0;

	for(i = 0; i < HT_FIR_FILTER_SIZE - 1; i++)
		ht->fir_buff[i] = ht->fir_buff[i + 1];

	ht->fir_buff[HT_FIR_FILTER_SIZE - 1] =
			sample * HT_FIR_FILTER_REVERSE_GAIN;

	for(i = 0; i <= HT_FIR_FILTER_TAPS; i++)
		out += ht->fir_coeffs[i] * ht->fir_buff[i];

	return out;
}






