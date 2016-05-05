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
	int i;
	double phase;

	if (osc == NULL)
		return -1;

	memset(osc, 0, sizeof(struct osc_state));
	osc->type = type;

#ifdef USE_WAVE_TABLE
	/* Put one full period on the wave table (fit 2*pi radians on
	 * WAVE_TABLE_SIZE slots) */
	switch (type) {
	case OSC_TYPE_SINE:
		for (i = 0; i < WAVE_TABLE_SIZE; i++) {
			phase = 2.0 * M_PI * ((double)i) /
					     ((double)WAVE_TABLE_SIZE);

			osc->wave_table[i] = sin(phase);

			/* In case we want to use cubic interpolation, also store the
			 * value of the derivative -cos(x)- */
#ifdef USE_CUBIC_INTERPOLATION
			osc->fdx[i] = cos(phase);
#endif
		}
		break;
	case OSC_TYPE_COSINE:
		for (i = 0; i < WAVE_TABLE_SIZE; i++) {
			phase = 2.0 * M_PI * ((double)i) /
					     ((double)WAVE_TABLE_SIZE);

			osc->wave_table[i] = cos(phase);

#ifdef USE_CUBIC_INTERPOLATION
			osc->fdx[i] = sin(phase);
#endif
		}
		break;
	default:
		return -1;
	}
#endif

	/*
	 * Due to Nyquist sampling theorem, the sample rate must be at
	 * least twice the frequency we want to sample. To make it more
	 * safe, check if the max supported frequency divided by
	 * the sample rate is an even number (a multiple of 2)
	 */
	if ((MAX_FREQUENCY >= sample_rate) ||
	    ((MAX_FREQUENCY / sample_rate) % 2))
		return -1;

	osc->sample_rate = sample_rate;

	/*
	 * On one second we will play <osc_sample_rate> samples, so if we
	 * want to play the sine wave we'll need to increase the phase on
	 * each sample in such a way so that it takes  <osc_sample_rate> slots
	 * to play the whole period. If we didn't had a wave table then we 'd
	 * increase the phase by 2 * pi / <osc_sample_rate> on each sample
	 * but here instead of 2 * pi we'll use the ONE_PERIOD macro so that
	 * when we use a wave table, this will represent the increments on
	 * the wave table instead (and one table holds one period).
	 */
	osc->phase_step = ((double)ONE_PERIOD) / ((double)osc->sample_rate);

	return 0;
}

/**
 * osc_increase_phase - Increase the current sine phase
 *
 */
void
osc_increase_phase(struct osc_state *osc)
{
	osc->current_phase += osc->phase_step;

	/* If we have exceeded one period (2 * pi)
	 * we need to rewind the current phase so that
	 * we don't go outside the table (since sin(2*p + a) = sin(a)
	 * we just subtract 2 * pi). */
	if (osc->current_phase >= (double)ONE_PERIOD)
		osc->current_phase -= (double)ONE_PERIOD;

	/* Make sure we didn't go too much (this will also catch
	 * the case of -0.0) */
	if (signbit(osc->current_phase))
		osc->current_phase = 0;

	return;
}

#ifdef USE_WAVE_TABLE
#ifdef USE_LINEAR_INTERPOLATION
/* Note: Linear interpolation is not worth it, it might actually
 * add artifacts and noise, especialy on sine waves. On my tests
 * the wave table alone had better FFT output than with linear
 * interpolation being used. It's also not that faster than our
 * cubic interpolation -with pre-calculated cosines- so I leave
 * it here only for reference. */

/**
 * osc_linear_interpolate -	Do a simple linear interpolation
 *				to get sine(phase) using the wavetable
 */
static double
osc_linear_interpolate(struct osc_state *osc, double phase)
{
	double x, x1, x2, y1, y2, slope;
	int y1idx, y2idx;

	x = fmod(phase, (double)ONE_PERIOD);

	/* Get the surounding points - wrap arround if needed */
	x1 = fmod(phase - osc->phase_step, (double)ONE_PERIOD);
	x2 = fmod(phase + osc->phase_step, (double)ONE_PERIOD);

	y1idx = (int)x1;
	y2idx = (int)x2;

	y1 = osc->wave_table[y1idx];
	y2 = osc->wave_table[y2idx];

	/* F(x) = ax + b
	 * F'(x) = a
	 *
	 * F(0) = b
	 * F'(0) = a
	 */

	return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
}
#endif

#ifdef USE_CUBIC_INTERPOLATION
/**
 * osc_cubic_interpolate -	Perform cubic interpolation to get
 *				sine(phase) using the wave table
 */
static double
osc_cubic_interpolate(struct osc_state *osc, double phase)
{
	double x, xtemp, xsq, xcub, y1, y2;
	double a, b, c, d;
	int x1, x2;

	/* Get the surounding points - wrap arround if needed */
	x1 = (int)(phase - osc->phase_step) % ONE_PERIOD;
	x2 = (int)(phase + osc->phase_step) % ONE_PERIOD;

	y1 = osc->wave_table[x1];
	y2 = osc->wave_table[x2];

	/* F(x) = ax^3 + bx^2 + cx + d
	 * F'(x) = 3ax^2 + 2bx + c
	 *
	 * Boundary conditions:
	 * F(0) = d
	 * F'(0) = c
	 * F(1) = a + b + c + d
	 * F'(1) = 3a + 2b +c
	 * 
	 * Solutions for the 4x4 system
	 * a = 2 * (F(0) - F(1)) + F'(0) + F'(1)
	 * b = 3 * (F(1) - F(0)) + 2F'(0) + F'(1)
	 * c = F(0)
	 * d = F'(0)
	 *
	 * Assuming F(x) behaves the same way between (x1,y1),
	 * (x2,y2) and (0,F(0)), (1,F(1)) -which in our case it does
	 * because F(x) = sin(x) or cos(x) for any x- we can use this
	 * solution for all points.
	 */
	a = 2.0 * (y1 - y2) + osc->fdx[x1] + osc->fdx[x2];
	b = 3.0 * (y2 - y1) - 2.0 * osc->fdx[x1] - osc->fdx[x2];
	c = osc->fdx[x1];
	d = y1;

	/* Since we work on [0,1] bring x there by subtracting x1 */
	xtemp = fmod(phase - osc->phase_step, (double)ONE_PERIOD);
	x = fmod(phase - xtemp, (double)ONE_PERIOD);
	xsq = pow(x, 2);
	xcub = pow(x, 3);

	/* Now that we know a, b, c, d, return F(x) */
	return (a * xcub + b * xsq + c * x + d);
}
#endif
#endif

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
double
osc_get_sample_for_freq(struct osc_state *osc, double freq)
{
	double phase = osc->current_phase * freq;

#ifdef USE_WAVE_TABLE
#if defined(USE_CUBIC_INTERPOLATION)
	return osc_cubic_interpolate(osc, phase);
#elif defined(USE_LINEAR_INTERPOLATION)
	return osc_linear_interpolate(osc, phase);
#else
	int table_slot;

	/* Make sure we don't exceed table size, % here is like
	 * a mask, if we get <X * ONE_PERIOD + something>, it'll
	 * keep <something> wich is the same as rewinding but for table
	 * slots (because ONE_PERIOD is the table size). */
	table_slot = ((int)phase) % ONE_PERIOD;

	return osc->wave_table[table_slot];
#endif

#else
	/* No need to rewind since sin()/cos() will loop anyway */
	switch (osc->type) {
	case OSC_TYPE_SINE:
		return sin(phase);
	case OSC_TYPE_COSINE:
		return cos(phase);
	default:
		return 0;
	}
#endif
}

/**
 * osc_get_19Khz_sample -	Get a 19KHz sample for the current
 *				phase.
 */
double
osc_get_19Khz_sample(struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, (double)19000.0);
}

/**
 * osc_get_38Khz_sample -	Get a 38KHz sample for the current
 *				phase.
 */
double
osc_get_38Khz_sample(struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, (double)38000.0);
}

/**
 * osc_get_57Khz_sample -	Get a 57KHz sample for the current
 *				phase.
 */
double
osc_get_57Khz_sample(struct osc_state *osc)
{
	return osc_get_sample_for_freq(osc, (double)57000.0);
}
