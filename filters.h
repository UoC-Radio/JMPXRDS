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
#include <jack/jack.h>		/* For jack-related types */
#include <pthread.h>		/* For pthread mutex / conditional */

/* A generic FFT low pass filter with optional pre-emphasis */
struct lpf_filter_data {
	uint16_t num_bins;
	uint16_t middle_bin;
	uint16_t cutoff_bin;
	double variance;
	uint32_t sample_rate;
	double	 bin_bw;
	double *filter_curve;
	fftw_complex *complex_buff;
	double *real_buff;
	fftw_plan dft_plan;
	fftw_plan ift_plan;
};

enum lpf_preemph_mode {
	LPF_PREEMPH_50US = 0,	/* E.U. / WORLD */
	LPF_PREEMPH_75US = 1,	/* U.S. */
	LPF_PREEMPH_NONE = 2,
};

void lpf_filter_destroy(struct lpf_filter_data *);
int lpf_filter_init(struct lpf_filter_data *, uint32_t, uint32_t, uint16_t);
int lpf_filter_apply(struct lpf_filter_data *, float*, float*, uint16_t, float, uint8_t);

/* Combined audio filter */
struct lpf_thread_data {
	struct lpf_filter_data *lpf;
	int *active;
	float *in;
	float *out;
	uint16_t num_samples;
	float gain;
	uint8_t preemph_tau;
	float peak_gain;
	pthread_mutex_t proc_mutex;
	pthread_cond_t proc_trigger;
	pthread_mutex_t done_mutex;
	pthread_cond_t done_trigger;
	jack_native_thread_t tid;
	int result;
};

struct audio_filter {
	struct lpf_thread_data lpftd_l;
	struct lpf_thread_data lpftd_r;
	struct lpf_filter_data audio_lpf_l;
	struct lpf_filter_data audio_lpf_r;
	jack_client_t *fmmod_client;
	int active;
};

void audio_filter_destroy(struct audio_filter *);
int audio_filter_init(struct audio_filter *, jack_client_t *,
		      uint32_t, uint32_t, uint16_t);
int audio_filter_apply(struct audio_filter *, float*, float *, float *, float *,
			uint16_t, float, uint8_t, uint8_t, float*, float*);

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
