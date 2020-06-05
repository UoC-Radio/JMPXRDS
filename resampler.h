/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Resampler
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
#include <soxr.h>		/* soxr types and macros */
#include <jack/jack.h>		/* For jack-related types */
#include <pthread.h>		/* For pthread mutex / conditional */

struct resampler_thread_data {
	soxr_t resampler;
	const float *in;
	float *out;
	uint32_t inframes;
	uint32_t outframes;
	size_t	frames_used;
	size_t	frames_generated;
	soxr_error_t result;
	pthread_mutex_t proc_mutex;
	pthread_cond_t proc_trigger;
	pthread_mutex_t done_mutex;
	pthread_cond_t done_trigger;
	jack_native_thread_t tid;
	int	*active;
};

struct resampler_data {
	jack_client_t *fmmod_client;
	uint32_t osc_samplerate;
	soxr_t audio_upsampler_l;
	soxr_t audio_upsampler_r;
	int audio_upsampler_bypass;
	soxr_t rds_upsampler;
	soxr_t mpx_downsampler;
	int mpx_downsampler_bypass;
	int active;
	struct resampler_thread_data rstd_l;
	struct resampler_thread_data rstd_r;
};

int resampler_init(struct resampler_data *rsmpl, uint32_t jack_samplerate,
		jack_client_t *fmmod_client, uint32_t osc_samplerate,
		uint32_t rds_samplerate, uint32_t output_samplerate);
int resampler_upsample_audio(struct resampler_data *rsmpl, const float *in_l,
			     const float *in_r, float *out_l, float *out_r,
			     uint32_t inframes, uint32_t outframes);
int resampler_upsample_rds(const struct resampler_data *rsmpl, const float *in, float *out,
			   uint32_t inframes, uint32_t outframes);
int resampler_downsample_mpx(const struct resampler_data *rsmpl, const float *in,
			     float *out, uint32_t inframes, uint32_t outframes);
void resampler_destroy(struct resampler_data *rsmpl);
