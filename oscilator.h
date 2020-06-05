/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Oscilator
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
#include "config.h"
#include <stdint.h>		/* For typed integers */

/* We want a clean 38KHz tone because we 'll use that for A.M.
 * After experimenting with different sample rates I ended up
 * using 4x oversampling (304KHz). I tried higher values but
 * this got me the cleanest FFT and nice waveforms, even for
 * 57KHz. However it was too hard on the CPU so I went for the
 * closest Greatest Common Factor between 38KHz and 57Khz which
 * is 228KHz (= 38 * 6 = 57 * 4). So it's 3x oversampling on
 * 38KHz and 2x on 57KHz.
 *
 * 57KHz waveform seems ok but the 38KHz never gets to 1 and there
 * is a spike generated at 57Khz at -60dB from the 19KHz pilot.
 * However RDS operation was fine during testing (since -60dB is
 * low enough and it's at the carrier). Stereo separation was
 * also fine so I'm leaving it to 228KHz for now.
 *
 * Don't change this unless you know what you are doing ! */
#define	OSC_SAMPLE_RATE		228000


/* That's the RDS carrier, we won't get any further than this */
#define MAX_FREQUENCY		57000

#define ONE_PERIOD		2.0 * M_PI


enum osc_type {
	OSC_TYPE_SINE = 0,
	OSC_TYPE_COSINE = 1,
};

struct osc_state {
	double phase_step;
	double current_phase;
	uint32_t sample_rate;
	int type;
};

int osc_initialize(struct osc_state *, uint32_t, int);
void osc_increase_phase(struct osc_state *);
void osc_shift_90deg(struct osc_state *sinwg);
float osc_get_sample_for_freq(const struct osc_state *osc, float freq);
float osc_get_19Khz_sample(struct osc_state *);
float osc_get_38Khz_sample(struct osc_state *);
float osc_get_57Khz_sample(struct osc_state *);
