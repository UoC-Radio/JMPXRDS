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
#include <stdint.h>		/* For typed integers */
#include <fftw3.h>		/* For FFTW support */

/* A generic FFT low pass filter */
struct lpf_filter_data {
	uint16_t num_bins;
	uint16_t cutoff_bin;
	uint32_t sample_rate;
	double *filter_curve;
	fftw_complex *complex_buff;
	double *real_buff;
	fftw_plan dft_plan;
	fftw_plan ift_plan;
};

void lpf_filter_destroy(struct lpf_filter_data *);
int lpf_filter_init(struct lpf_filter_data *, uint32_t, uint32_t, uint16_t);
int lpf_filter_apply(struct lpf_filter_data *, float*, float*, uint16_t, float);

/* FM Preemphasis IIR filter
 * Only used as part of the combined audio filter- */
struct fmpreemph_filter_data {
	float iir_inbuff_l[2];
	float iir_outbuff_l[2];
	float iir_inbuff_r[2];
	float iir_outbuff_r[2];
	float iir_ataps[3];
	float iir_btaps[2];
};

/* Combined audio filter */
struct audio_filter {
	struct lpf_filter_data audio_lpf;
	struct fmpreemph_filter_data fm_preemph;
};

void audio_filter_destroy(struct audio_filter *aflt);
int audio_filter_init(struct audio_filter *, uint32_t, uint32_t, uint16_t, uint8_t);
int audio_filter_apply(struct audio_filter *, float*, float *, float *, float *,
			uint16_t, float, uint8_t);

/* IIR filter for the Weaver modulator (SSB) */
#define WEAVER_FILTER_TAPS 10
#define	WEAVER_FILTER_SIZE WEAVER_FILTER_TAPS + 1
#define WEAVER_FILTER_REVERSE_MAX_GAIN (1.0 / 5.279294303e+02)

struct ssb_filter_data {
	float iir_inbuff_l[WEAVER_FILTER_SIZE];
	float iir_outbuff_l[WEAVER_FILTER_SIZE];
	float iir_inbuff_r[WEAVER_FILTER_SIZE];
	float iir_outbuff_r[WEAVER_FILTER_SIZE];
	/* ataps are symmetric, store the bottom
	 * half part */
	float iir_ataps[WEAVER_FILTER_TAPS / 2 + 1];
	float iir_btaps[WEAVER_FILTER_TAPS];
};

int iir_ssb_filter_init(struct ssb_filter_data *);
float iir_ssb_filter_apply(struct ssb_filter_data *, float, uint8_t);

/* Hilbert transformer for the Hartley modulator (SSB) */
struct hilbert_transformer_data {
	uint16_t num_bins;
	fftw_complex *complex_buff;
	double *real_buff;
	fftw_plan dft_plan;
	fftw_plan ift_plan;
};

int hilbert_transformer_init(struct hilbert_transformer_data *ht, uint16_t);
void hilbert_transformer_destroy(struct hilbert_transformer_data *ht);
int hilbert_transformer_apply(struct hilbert_transformer_data *ht, float *, uint16_t);
