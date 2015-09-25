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

#include <stdlib.h> /* For NULL */
#include <string.h> /* For memset */
#include <math.h> /* For sin, cos and M_PI */
#include "filters.h"

/*****************************\
* GENERIC FIR LOW-PASS FILTER *
\*****************************/

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
sinc_filter(double fc_doubled, int bin)
{
	return sinc(fc_doubled * (double)(bin - FIR_FILTER_HALF_SIZE));
}

/* Blackman - Harris window */
static double inline
blackman_window(int bin)
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
fir_filter_init(struct fir_filter_data *fir, int cutoff_freq, int sample_rate)
{
	int i = 0;
	double fc_doubled = 0;

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

	return 0;
}

float
fir_filter_apply(struct fir_filter_data *fir, float sample, int chan_idx)
{
	int i;
	float out = 0;
	float *fir_buf = NULL;
	int previous, later;

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
		out += fir->fir_coeffs[i] * (fir_buf[previous] +
						fir_buf[later]);
		later++;
		if(later >= FIR_FILTER_SIZE)
			later = 0;
	}

	return out;
}

void
fir_filter_update(struct fir_filter_data *fir)
{
	fir->fir_index++;
	if(fir->fir_index >= FIR_FILTER_SIZE)
		fir->fir_index = 0;
}


/**********************************\
* FM IIR PRE-EMPHASIS AUDIO FILTER *
\**********************************/

static int
fmpreemph_filter_init(struct fmpreemph_filter_data *iir, int cutoff_freq,
		int sample_rate, int preemph_tau_usecs)
{
	double tau[2] = {0};
	double edge[2] = {0};

	/* Now for the IIR preemphasis filter.
	 * This comes mostly from GNU Radio's
	 * fm_emph.py */
	tau[0] = 0.000001 * (double) preemph_tau_usecs;
	tau[1] = tau[0] / ((2.0 * M_PI * ((double) cutoff_freq) * tau[0]) - 1);
	edge[0] = tan(1 / ((2.0 * (double) sample_rate) * tau[0]));
	edge[1] = tan(1 / ((2.0 * (double) sample_rate) * tau[1]));
	iir->iir_btaps[0] = (edge[1] + 1) / (1 + edge[0] + edge[1]);
	iir->iir_btaps[1] = (edge[1] - 1) / (1 + edge[0] + edge[1]);
	iir->iir_ataps[0] = 1;
	iir->iir_ataps[1] = (edge[0] + edge[1] - 1) / (edge[0] + edge[1] + 1);

	return 0;
}

static float
fmpreemph_filter_apply(struct fmpreemph_filter_data *iir, float sample,
	int chan_idx)
{
	float out = 0;
	float *iir_inbuf = NULL;
	float *iir_outbuf = NULL;

	if(chan_idx == 0) {
		iir_inbuf = iir->iir_inbuff_l;
		iir_outbuf = iir->iir_outbuff_l;
	} else if(chan_idx == 1) {
		iir_inbuf = iir->iir_inbuff_r;
		iir_outbuf = iir->iir_outbuff_r;
	}

	/* Apply the IIR FM Pre-emphasis filter to increase the
	 * gain as the frequency gets higher and compensate for
	 * the increased noise on higher frequencies. A de-emphasis
	 * filter is used on the receiver to undo that */
	iir_inbuf[1] = sample;
	iir_outbuf[1] = iir->iir_ataps[0] * iir_inbuf[1] +
			iir->iir_ataps[1] * iir_inbuf[0] +
			iir->iir_btaps[0] * iir_outbuf[1] +
			iir->iir_btaps[1] * iir_outbuf[0];
	iir_inbuf[0] = iir_inbuf[1];
	iir_outbuf[0] = iir_outbuf[1];
	out = iir_outbuf[0];

	return out;
}


/***********************************************\
* COMBINED AUDIO FILTER (FM PRE-EMPHASIS + LPF) *
\***********************************************/

void
audio_filter_init(struct audio_filter *aflt, int cutoff_freq, int sample_rate,
	int preemph_tau_usecs)
{
	fir_filter_init(&aflt->audio_lpf, cutoff_freq, sample_rate);
	fmpreemph_filter_init(&aflt->fm_preemph, cutoff_freq, sample_rate,
							preemph_tau_usecs);
}

void
audio_filter_update(struct audio_filter *aflt)
{
	fir_filter_update(&aflt->audio_lpf);
}

float
audio_filter_apply(struct audio_filter *aflt, float sample, int chan_idx)
{
	float out = 0;

	out = fir_filter_apply(&aflt->audio_lpf, sample, chan_idx);
	out = fmpreemph_filter_apply(&aflt->fm_preemph, out, chan_idx);

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

/* Coefficients and the SSB_FILTER_GAIN macro were calculated using
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
						int chan_idx)
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
	}

	/* Make room for the new in sample */
	for(i = 0; i < SSB_FILTER_SIZE - 1; i++)
		iir_inbuf[i] = iir_inbuf[i + 1];

	/* Put it on the filter's ring buffer */
	iir_inbuf[SSB_FILTER_SIZE - 1] = sample * SSB_FILTER_REVERSE_GAIN;


	/* Process input ring buffer */
	for(i = 0; i < SSB_FILTER_TAPS / 2; i++)
		out_sample += iir->iir_ataps[i] * (iir_inbuf[i]
	+ iir_inbuf[SSB_FILTER_TAPS - i]);
	out_sample += iir->iir_ataps[SSB_FILTER_TAPS / 2] *
	iir_inbuf[SSB_FILTER_TAPS / 2];

	/* Make room for the new out sample */
	for(i = 0; i < SSB_FILTER_SIZE - 1; i++)
		iir_outbuf[i] = iir_outbuf[i + 1];

	/* Process output ring buffer */
	for(i = 0; i < SSB_FILTER_TAPS; i++)
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






