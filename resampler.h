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

#include <samplerate.h> /* src_* functions and macros */
#include "filters.h"

struct resampler_data {
	float *upsampled_audio;
	int upsampled_audio_len;
	SRC_STATE* upsampler_state;
	SRC_DATA upsampler_data;
	double upsampler_ratio;
	SRC_STATE* downsampler_state;
	SRC_DATA downsampler_data;
	double downsampler_ratio;
	struct fir_filter_data mpx_lpf;
};

int resampler_init(struct resampler_data *rsmpl, int jack_samplerate,
			int osc_sample_rate, int max_process_frames);
float* resampler_upsample_audio(struct resampler_data *rsmpl, float *in,
						int inframes, int *ret);
float* resampler_downsample_mpx(struct resampler_data *rsmpl, float *in,
					float *out, int inframes, int *ret);
void resampler_destroy(struct resampler_data *rsmpl);
