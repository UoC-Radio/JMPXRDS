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

/* FM Preemphasis IIR filter */
struct fmpreemph_filter_data {
	float last_in;
	float last_out[2];
	float ataps_50[2];
	float btaps_50[2];
	float ataps_75[2];
	float btaps_75[2];
};

/* A generic FFT low pass filter */
struct lpf_filter_data {
	uint16_t num_bins;
	uint16_t middle_bin;
	uint16_t cutoff_bin;
	float variance;
	uint32_t sample_rate;
	float bin_bw;
	float *filter_curve;
	fftwf_complex *complex_buff;
	float *real_buff;
	fftwf_plan dft_plan;
	fftwf_plan ift_plan;
};

enum lpf_preemph_mode {
	LPF_PREEMPH_50US = 0,	/* E.U. / WORLD */
	LPF_PREEMPH_75US = 1,	/* U.S. */
	LPF_PREEMPH_NONE = 2,
};

void lpf_filter_destroy(struct lpf_filter_data *);
int lpf_filter_init(struct lpf_filter_data *, uint32_t, uint32_t, uint16_t);
int lpf_filter_apply(struct lpf_filter_data *, float*, float*, uint16_t, float);

/* Combined audio filter */
struct aflt_thread_data {
	struct lpf_filter_data *lpf;
	struct fmpreemph_filter_data *fmprf;
	int *active;
	float *in;
	float *out;
	uint16_t num_samples;
	float gain;
	enum lpf_preemph_mode preemph_tau_mode;
	float peak_gain;
	pthread_mutex_t proc_mutex;
	pthread_cond_t proc_trigger;
	pthread_mutex_t done_mutex;
	pthread_cond_t done_trigger;
	jack_native_thread_t tid;
	int result;
};

struct audio_filter {
	struct aflt_thread_data afltd_l;
	struct aflt_thread_data afltd_r;
	struct lpf_filter_data audio_lpf_l;
	struct lpf_filter_data audio_lpf_r;
	struct fmpreemph_filter_data fmprf_l;
	struct fmpreemph_filter_data fmprf_r;
	jack_client_t *fmmod_client;
	int active;
};

void audio_filter_destroy(struct audio_filter *);
int audio_filter_init(struct audio_filter *, jack_client_t *,
		      uint32_t, uint32_t, uint16_t);
int audio_filter_apply(struct audio_filter *, float*, float *, float *, float *,
			uint16_t, float, uint8_t, enum lpf_preemph_mode, float*, float*);

/* Hilbert transformer for the Hartley modulator (SSB) */
struct hilbert_transformer_data {
	uint16_t num_bins;
	fftwf_complex *complex_buff;
	float *real_buff;
	fftwf_plan dft_plan;
	fftwf_plan ift_plan;
};

int hilbert_transformer_init(struct hilbert_transformer_data *ht, uint16_t);
void hilbert_transformer_destroy(struct hilbert_transformer_data *ht);
int hilbert_transformer_apply(struct hilbert_transformer_data *ht, float *, uint16_t);
