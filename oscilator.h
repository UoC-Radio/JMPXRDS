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

#ifdef USE_WAVE_TABLE
	/* On my experiments 64 was enough to get a decent
	 * sine wave for all 3 frequencies. I used powers of
	 * two so that % ONE_PERIOD changes to an & operator
	 * -the compiler does that automaticaly- which greatly
	 * improves performance */
#define WAVE_TABLE_SIZE		63
#define ONE_PERIOD		WAVE_TABLE_SIZE
#else
#define ONE_PERIOD		2 * M_PI
#endif

enum osc_type {
	OSC_TYPE_SINE = 0,
	OSC_TYPE_COSINE = 1,
};

struct osc_state {
#ifdef	USE_WAVE_TABLE
	double wave_table[WAVE_TABLE_SIZE];

	/* For cubic interpolation we need to know not only the
	 * values of f(xi) but also the values of f'(xi). Since we
	 * know that f(x) is sin(x) or cos(x) we can just create a
	 * lookup table to store the values of f(x)' and be
	 * more accurate and fast. */
#ifdef	USE_CUBIC_INTERPOLATION
	double fdx[WAVE_TABLE_SIZE];
#endif

#endif
	double phase_step;
	double current_phase;
	uint32_t sample_rate;
	int type;
};

int osc_initialize(struct osc_state *, uint32_t, int);
void osc_increase_phase(struct osc_state *);
void osc_shift_90deg(struct osc_state *sinwg);
double osc_get_sample_for_freq(struct osc_state *osc, double freq);
double osc_get_19Khz_sample(struct osc_state *);
double osc_get_38Khz_sample(struct osc_state *);
double osc_get_57Khz_sample(struct osc_state *);
