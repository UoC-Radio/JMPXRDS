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
#include "oscilator.h"
#include <stdlib.h>		/* For NULL */
#include <math.h>		/* For sin, cos, M_PI, fmod and signbit */
#include <string.h>		/* For memset */

/***********\
* OSCILATOR *
\***********/

/*
 * This is the implementation of a sine wave generator that
 * produces 3 phase-synced sine waves of frequencies 19KHz,
 * 38KHz and 57KHz, used to create the FM MPX signal. It can
 * also produce a cosine the same way for use in SSB modulation.
 */

/**
 * osc_initialize_state - Initialize the oscilator's state
 *
 */
int
osc_initialize(struct osc_state *osc, uint32_t sample_rate, int type)
{
	if (osc == NULL)
		return -1;

	memset(osc, 0, sizeof(struct osc_state));
	osc->type = type;

	/*
	 * Due to Nyquist sampling theorem, the sample rate must be at
	 * least twice the frequency we want to sample. To make it more
	 * safe, check if the max supported frequency divided by
	 * the sample rate is an even number (a multiple of 2)
	 */
	if ((MAX_FREQUENCY >= sample_rate) ||
	    ((MAX_FREQUENCY / sample_rate) & 1))
		return -3;

	osc->sample_rate = sample_rate;

	/*
	 * On each second we will play <osc_sample_rate> samples, so if we
	 * want to play the sine wave we'll need to increase the phase on
	 * each sample in such a way so that its period (1sec) fits within
	 * <osc_sample_rate> slots.
	 */
	osc->phase_step = ((double) (ONE_PERIOD)) / ((double) osc->sample_rate);

	return 0;
}

/**
 * osc_increase_phase - Increase the current sine phase
 *
 */
void
osc_increase_phase(struct osc_state *osc)
{
	/* Make sure we don't exceed one period
	 * note that sin/cos will not have an issue since they'll
	 * rewind themselves, however we risk an overflow of
	 * current_phase so be on the safe side. This will also
	 * take care of current_phase's sign since fmod will return
	 * a value with the same sign. */
	osc->current_phase = fmod(osc->current_phase + osc->phase_step,
				  (double) (ONE_PERIOD));

	return;
}

/*
 * On the functions below we want to get some sine waves of a specific
 * frequency that are all phase-synced (that means they all start at
 * the same time or -in our case- from the same phase). To do that we'll
 * need to playback a sine period <frequency> times faster, so we multiply
 * the phase with the frequency.
 */

/**
 * osc_get_sample_for_freq -	Get a sample for the given frequency
 *				at the current phase.
 */
float
osc_get_sample_for_freq(const struct osc_state *osc, float freq)
{
	double phase = osc->current_phase * (double) freq;

	switch (osc->type) {
	case OSC_TYPE_SINE:
		return (float) sin(phase);
	case OSC_TYPE_COSINE:
		return (float) cos(phase);
	default:
		return 0;
	}
}

/**
 * osc_get_19Khz_sample -	Get a 19KHz sample for the current
 *				phase.
 */
float
osc_get_19Khz_sample(const struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, 19000.0);
}

/**
 * osc_get_38Khz_sample -	Get a 38KHz sample for the current
 *				phase.
 */
float
osc_get_38Khz_sample(const struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, 38000.0);
}

/**
 * osc_get_57Khz_sample -	Get a 57KHz sample for the current
 *				phase.
 */
float
osc_get_57Khz_sample(const struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, 57000.0);
}
