/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RDS Encoder
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
#include "resampler.h"
#include "utils.h"
#include "rds_encoder.h"
#include <time.h>		/* For gmtime, localtime etc (group 4A) */
#include <arpa/inet.h>		/* For htons() */
#include <string.h>		/* For memset/memcpy/strnlen */
#include <stdlib.h>		/* For malloc/free */
#include <stdio.h>		/* For printf */
#include <unistd.h>		/* For ftruncate(), close() */
#include <sys/mman.h>		/* For shm_open */
#include <sys/stat.h>		/* For mode constants */
#include <fcntl.h>		/* For O_* and F_* constants */
#include <math.h>		/* For fabs */
#include <jack/thread.h>	/* For thread handling through jack */
#include <signal.h>		/* For raise() */

/*********\
* HELPERS *
\*********/

static inline int
num_resampled_samples(int in_srate, int out_srate, int num_samples)
{
	float ratio = (float) out_srate / (float) in_srate;
	float olenf = ratio * ((float) num_samples);
	/* Also cover the case where out_srate < in_srate */
	olenf = fmax(olenf, num_samples - 1.0);
	return (int) olenf;
}

/************\
* MODULATION *
\************/

/* Loockup table with waveforms of pre-calculated biphase encoded and
 * filtered symbols. For more infos on this visit
 * http://www.langmeier.ch/docs/rds-biphase.pdf
 * 
 * Think of it as a moving 3bit window, for each new bit that comes in,
 * the window's value is used as an index to this table and the matching
 * waveform is appended to the previous one, creating a continuous waveform.
 *
 * E.g for the sequence "0100101110100010":
 *
 * [010] -> output waveform symbol_waveforms[2]
 * [100] -> output waveform symbol_waveforms[4]
 * [001] -> output waveform symbol_waveforms[1]
 * ...
 * [000] -> output waveform symbol_waveforms[0]
 * [001] -> output waveform symbol_waveforms[1]
 * [010] -> output waveform symbol_waveforms[2]
 *
 * Notice "[010]0101110100010" -> "0[100]101110100010" -> ...
 */
float symbol_waveforms[8][40] = {
	{-0.077944, -0.231941, -0.380444, -0.519913, -0.646931, -0.758415,
	 -0.851558, -0.923948, -0.973663, -0.999298, -1.000000, -0.975585,
	 -0.926511, -0.853877, -0.759453, -0.647450, -0.521073, -0.381756,
	 -0.232917, -0.078280, +0.078280, +0.232917, +0.381756, +0.521073,
	 +0.647450, +0.759453, +0.853877, +0.926511, +0.975585, +1.000000,
	 +0.999298, +0.973663, +0.923948, +0.851558, +0.758415, +0.646931,
	 +0.519913, +0.380444, +0.231941, +0.077944},

	{-0.077944, -0.231941, -0.380444, -0.519913, -0.646931, -0.758415,
	 -0.851558, -0.923948, -0.973663, -0.999298, -1.000000, -0.975585,
	 -0.926511, -0.853877, -0.759453, -0.643818, -0.509690, -0.362621,
	 -0.206824, -0.046876, +0.112461, +0.266518, +0.410932, +0.541642,
	 +0.655263, +0.750847, +0.826167, +0.878414, +0.907712, +0.915311,
	 +0.903409, +0.875057, +0.834101, +0.784753, +0.731498, +0.678793,
	 +0.630879, +0.591418, +0.563372, +0.548814},

	{+0.548814, +0.563372, +0.591418, +0.630879, +0.678793, +0.731498,
	 +0.784753, +0.834101, +0.875057, +0.903409, +0.915311, +0.907712,
	 +0.878414, +0.826167, +0.750847, +0.651601, +0.530259, +0.391766,
	 +0.240425, +0.081057, -0.081057, -0.240425, -0.391766, -0.530259,
	 -0.651601, -0.750847, -0.826167, -0.878414, -0.907712, -0.915311,
	 -0.903409, -0.875057, -0.834101, -0.784753, -0.731498, -0.678793,
	 -0.630879, -0.591418, -0.563372, -0.548814},

	{+0.548814, +0.563372, +0.591418, +0.630879, +0.678793, +0.731498,
	 +0.784753, +0.834101, +0.875057, +0.903409, +0.915311, +0.907712,
	 +0.878414, +0.826167, +0.750847, +0.655263, +0.541642, +0.410932,
	 +0.266518, +0.112461, -0.046876, -0.206824, -0.362621, -0.509690,
	 -0.643818, -0.759453, -0.853877, -0.926511, -0.975585, -1.000000,
	 -0.999298, -0.973663, -0.923948, -0.851558, -0.758415, -0.646931,
	 -0.519913, -0.380444, -0.231941, -0.077944},

	{-0.548814, -0.563372, -0.591418, -0.630879, -0.678793, -0.731498,
	 -0.784753, -0.834101, -0.875057, -0.903409, -0.915311, -0.907712,
	 -0.878414, -0.826167, -0.750847, -0.655263, -0.541642, -0.410932,
	 -0.266518, -0.112461, +0.046876, +0.206824, +0.362621, +0.509690,
	 +0.643818, +0.759453, +0.853877, +0.926511, +0.975585, +1.000000,
	 +0.999298, +0.973663, +0.923948, +0.851558, +0.758415, +0.646931,
	 +0.519913, +0.380444, +0.231941, +0.077944},

	{-0.548814, -0.563372, -0.591418, -0.630879, -0.678793, -0.731498,
	 -0.784753, -0.834101, -0.875057, -0.903409, -0.915311, -0.907712,
	 -0.878414, -0.826167, -0.750847, -0.651601, -0.530259, -0.391766,
	 -0.240425, -0.081057, +0.081057, +0.240425, +0.391766, +0.530259,
	 +0.651601, +0.750847, +0.826167, +0.878414, +0.907712, +0.915311,
	 +0.903409, +0.875057, +0.834101, +0.784753, +0.731498, +0.678793,
	 +0.630879, +0.591418, +0.563372, +0.548814},

	{+0.077944, +0.231941, +0.380444, +0.519913, +0.646931, +0.758415,
	 +0.851558, +0.923948, +0.973663, +0.999298, +1.000000, +0.975585,
	 +0.926511, +0.853877, +0.759453, +0.643818, +0.509690, +0.362621,
	 +0.206824, +0.046876, -0.112461, -0.266518, -0.410932, -0.541642,
	 -0.655263, -0.750847, -0.826167, -0.878414, -0.907712, -0.915311,
	 -0.903409, -0.875057, -0.834101, -0.784753, -0.731498, -0.678793,
	 -0.630879, -0.591418, -0.563372, -0.548814},

	{+0.077944, +0.231941, +0.380444, +0.519913, +0.646931, +0.758415,
	 +0.851558, +0.923948, +0.973663, +0.999298, +1.000000, +0.975585,
	 +0.926511, +0.853877, +0.759453, +0.647450, +0.521073, +0.381756,
	 +0.232917, +0.078280, -0.078280, -0.232917, -0.381756, -0.521073,
	 -0.647450, -0.759453, -0.853877, -0.926511, -0.975585, -1.000000,
	 -0.999298, -0.973663, -0.923948, -0.851558, -0.758415, -0.646931,
	 -0.519913, -0.380444, -0.231941, -0.077944}
};

/* Offset words used for calculating checkwords(Anex A, table A.1) */
static uint16_t offset_words[] = { 0x0FC,	//      A
	0x198,			//      B
	0x168,			//      C
	0x1B4,			//      D
	0x350,			//      C'
};

#define RDS_ALT_OFFSET_WORD_C_IDX	4
/* Note: Offset word E (all zeroes) is deprecated */

/* Generator matrix for the CRC polynomial
 * g(x) = x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1 (0x1B9)
 * (Anex B, figure B.1) - This is used to make CRC calculations
 * faster (it's basicaly 0x1B9, "shifted" 15 times). */
static uint16_t G[] = { 0x1B9, 0x372, 0x35D, 0x303, 0x3BF,
	0x2C7, 0x037, 0x06E, 0x0DC, 0x1B8,
	0x370, 0x359, 0x30B, 0x3AF, 0x2E7,
	0x077
};

/* Generate a block's checkword and
 * assemble the block as a 32bit int */
static uint32_t
rds_generate_block(struct rds_block *block)
{
	uint32_t encoded_block = 0;
	int i = 0;

	for (i = RDS_INFOWORD_SIZE_BITS - 1; i >= 0; i--)
		if (block->infoword & (1 << i))
			block->checkword ^= G[i];
	block->checkword ^= block->offset_word;

	encoded_block |= block->checkword | (block->infoword << 10);

	return encoded_block;
}

/* Get a group and generate its waveform (this is
 * where the modulation happens as described on
 * section 4 of the standard) */
static int
rds_generate_group_samples(struct rds_group *group)
{
	static uint8_t moving_window = 0;
	uint8_t current_bit = 0;
	uint8_t previous_bit = 0;
	uint32_t current_block = 0;
	float *buffer = group->samples_buffer;
	uint16_t buffer_offset = 0;
	int i = 0;
	int j = 0;

	for (i = 0; i < RDS_BLOCKS_PER_GROUP; i++) {
		current_block = rds_generate_block(&group->blocks[i]);
		for (j = RDS_BLOCK_SIZE_BITS - 1; j >= 0; j--) {
			previous_bit = (moving_window & 1);
			current_bit = (current_block & (1 << j)) ? 1 : 0;
			/* Differential coding (Section 4.7) */
			current_bit ^= previous_bit;
			/* Put current bit on the window and
			 * append the waveform */
			moving_window <<= 1;
			moving_window |= current_bit;
			memcpy(buffer + buffer_offset,
			       &symbol_waveforms[moving_window & 0x7][0],
			       RDS_SAMPLES_PER_SYMBOL * sizeof(float));
			buffer_offset += RDS_SAMPLES_PER_SYMBOL;
		}
	}

	return 0;
}

/******************\
* GROUP GENERATION *
\******************/

/* Group 0A/0B: Basic tuning and switching information
 * (Section 6.1.5.1) */
static int
rds_generate_group_0(const struct rds_encoder *enc, struct rds_group *group,
		     uint8_t version)
{
	struct rds_encoder_state *st = enc->state;
	uint16_t temp_infoword = 0;

	/*
	 * Block 2 end -> one bit for TA, one bit for MS,
	 *              one bit from the 4bit DI field in reverse order
	 *              (bit number maps to the idx so it's 0 - 3), and
	 *              2 bits for the ps index.
	 */
	temp_infoword = st->ps_idx | ((st->di >> (3 - st->ps_idx)) & 1) << 2 |
			(st->ms & 1) << 3 | (st->ta & 1) << 4;
	group->blocks[1].infoword |= temp_infoword;

	/* On Version A, 3rd block contains the AF information, we assume
	 * the data on the af array are properly formatted according to
	 * section 6.2.1.6 We only support method A */
	if (version == RDS_GROUP_VERSION_A) {
		group->blocks[2].infoword = (st->af_data[st->af_idx] << 8) |
					     st->af_data[st->af_idx + 1];
		if (st->af_idx >= 10)
			st->af_idx = 0;
		else
			st->af_idx += 2;
		group->blocks[2].infoword = temp_infoword;
	}

	/* 
	 * Block 4 -> 2 characters from PS (depending on idx)
	 *              (It takes 4 0A/0B groups to transmit the
	 *              full DI and PS fields)
	 */
	group->blocks[3].infoword = (st->ps[2 * st->ps_idx] << 8) |
				     st->ps[2 * st->ps_idx + 1];
	if (st->ps_idx >= 3)
		st->ps_idx = 0;
	else
		st->ps_idx++;

	return 0;
}

/* Group 1A/B PIN and Slow labeling codes
 * (Section 6.1.5.2) */
static int
rds_generate_group_1(const struct rds_encoder *enc, struct rds_group *group,
		     uint8_t version)
{
	const struct rds_encoder_state *st = enc->state;
	static int vcode = 0;

	/*
	 * We only do this for Extended Country Code (ECC) and
	 * Language Identification Cod e(LIC) on block 3, anything
	 * else is not supported
	 */
	if (version != RDS_GROUP_VERSION_A)
		return -1;

	/*
	 * Block 2 end -> Radio Paging Codes (not supported)
	 */

	/*
	 * Block 3 -> first bit is the Link Actuator (LA) (not usded)
	 *              then comes the variant code (it's 0 for ECC
	 *              and 3 for LIC)
	 */
	vcode = (vcode == 0) ? 3 : 0;
	group->blocks[2].infoword = (vcode == 0 ? st->ecc & 0xFF :
				     st->lic & 0xFFF) | (vcode << 12);

	return 0;
}

/* Group 2A/2B: RadioText
 * (Section 6.1.5.3) */
static int
rds_generate_group_2(const struct rds_encoder *enc, struct rds_group *group,
		     uint8_t version)
{
	struct rds_encoder_state *st = enc->state;
	uint16_t temp_infoword = 0;

	/*
	 * Block 2 end -> one bit for the A/B flag (changing this clears
	 *              the RT buffer on the receiver) next 4 bits is the
	 *              index (0 - 14)
	 */
	temp_infoword = (st->rt_idx & 0xF) | (st->rt_flush & 1) << 4;
	group->blocks[1].infoword |= temp_infoword;

	/*
	 * Version A:
	 * Block 3/4 -> 2 characters from the RT
	 * (a total of 64 characters per message)
	 *
	 * Version B:
	 * 4th block -> 2 characters from the RT
	 * (a total of 32 characters per message)
	 *
	 * Note:
	 * It's not possible to mix A and B groups, either all message
	 * is sent by using 2As or 2Bs
	 *
	 * To make things easier we only support version A here. This
	 * way we won't need to verify if version changed when transmitting
	 * a message + we'll get the maximum message length.
	 */
	if (version != RDS_GROUP_VERSION_A)
		return -1;

	group->blocks[2].infoword = st->rt[4 * st->rt_idx] << 8 |
				    st->rt[4 * st->rt_idx + 1];

	group->blocks[3].infoword = st->rt[4 * st->rt_idx + 2] << 8 |
				    st->rt[4 * st->rt_idx + 3];

	st->rt_idx++;

	if (st->rt_idx >= st->rt_segments)
		st->rt_idx = 0;

	return 0;
}

/* Group 4A: Clock-time and date
 * (Section 6.1.5.6) */
static int
rds_generate_group_4(__attribute__((unused)) const struct rds_encoder *enc,
		     struct rds_group *group, uint8_t version)
{
	struct tm utc;
	struct tm local_time;
	double tz_offset = 0;
	time_t now;
	uint16_t temp_infoword = 0;
	int min = 0;
	int hour = 0;
	int day = 0;
	int month = 0;
	int year = 0;
	int leap_day = 0;
	int mjd = 0;

	/* Group 4B is Open Data and it's not supported */
	if (version != RDS_GROUP_VERSION_A)
		return -1;

	time(&now);

	if(!gmtime_r(&now, &utc))
		return -2;

	if(!localtime_r(&now, &local_time))
		return -3;

	min = utc.tm_min;
	hour = utc.tm_hour;
	day = utc.tm_mday;
	month = utc.tm_mon + 1;
	year = utc.tm_year;
	tz_offset = local_time.tm_hour - hour;

	if (month <= 2)
		leap_day = 1;

	/* Formula from Anex G */
	mjd = 14956 + day + (int)((year - leap_day) * 365.25)
	    + (int)((month + 1 + (leap_day * 12)) * 30.6001);

	/*
	 * Block 2 end -> first 2 bits of mjd
	 */
	temp_infoword = (mjd >> 15) & 0x3;
	group->blocks[1].infoword |= temp_infoword;

	/*
	 * Block 3 -> Rest bits of mjd and first bit of hour
	 */
	group->blocks[2].infoword = ((mjd << 1) & 0xFFFE) | ((hour >> 4) & 0x1);

	/*
	 * Block 4 -> Rest of hour bits, minutes and local
	 *              time offset (sign and value)
	 */
	group->blocks[3].infoword = ((hour & 0xf) << 12) |
				    ((min & 0x1f) << 6) |
				    ((tz_offset > 0 ? 0 : 1) << 5) |
				    ((int)fabs(2 * tz_offset) & 0x1f);

	return 0;
}

/* Group 10A: Programme type name (PTYN)
 * (Section 6.1.5.14) */
static int
rds_generate_group_10(const struct rds_encoder *enc, struct rds_group *group,
		      uint8_t version)
{
	struct rds_encoder_state *st = enc->state;
	int temp_infoword = 0;

	/* Group 10B is Open Data and it's not supported */
	if (version != RDS_GROUP_VERSION_A)
		return -1;

	/*
	 * Block 2 end -> A/B (flush) flag, 3 zeroes and the 1bit index
	 */
	temp_infoword = st->ptyn_idx | st->ptyn_flush << 4;
	group->blocks[1].infoword |= temp_infoword;

	/*
	 * Block 3/4 -> 2 characters from the PTYN
	 */
	group->blocks[2].infoword = st->ptyn[4 * st->ptyn_idx] << 8 |
				    st->ptyn[4 * st->ptyn_idx + 1];

	group->blocks[3].infoword = st->ptyn[4 * st->ptyn_idx + 2] << 8 |
				    st->ptyn[4 * st->ptyn_idx + 3];

	if (st->ptyn_idx == 1)
		st->ptyn_idx = 0;
	else
		st->ptyn_idx = 1;

	return 0;
}

/* Group 15B: Fast basic tuning and switching information
 * (Section 6.1.5.21) */
static int
rds_generate_group_15(const struct rds_encoder *enc, struct rds_group *group,
		      uint8_t version)
{
	struct rds_encoder_state *st = enc->state;
	uint16_t temp_infoword = 0;

	/*
	 * Block 2 end -> one bit for TA, one bit for MS,
	 *              one bit from the 4bit DI field in reverse order
	 *              (bit number maps to the idx so it's 0 - 3), and
	 *              2 bits for the ps index.
	 */
	temp_infoword = st->ps_idx | ((st->di >> (3 - st->ps_idx)) & 1) << 2 |
			(st->ms & 1) << 3 | (st->ta & 1) << 4;
	group->blocks[1].infoword |= temp_infoword;

	/* Group 15A is Open Data and it's not supported */
	if (version != RDS_GROUP_VERSION_B)
		return -1;

	/* 
	 * Block 4 -> A copy of block 2
	 */
	group->blocks[3].infoword = group->blocks[1].infoword;

	/* Use PS index for this one too since we send this only if PS
	 * is not set (if it is we send a 0B instead) */
	if (st->ps_idx >= 3)
		st->ps_idx = 0;
	else
		st->ps_idx++;

	return 0;
}

/* Wrapper to handle common group characteristics and call
 * the propper group-specific function */
static int
rds_generate_group(const struct rds_encoder *enc, struct rds_group *group,
		   uint8_t code, uint8_t version)
{
	const struct rds_encoder_state *st = enc->state;
	int i = 0;
	int ret = 0;

	if (unlikely(enc == NULL || group == NULL || version > RDS_GROUP_VERSION_MAX))
		return -1;

	memset(group, 0, sizeof(struct rds_group));

	/* Fill in the offset words for each block */
	for (i = 0; i < RDS_BLOCKS_PER_GROUP; i++)
		group->blocks[i].offset_word = offset_words[i];

	/*
	 * For every group:
	 * First block -> PI code
	 * Second block -> First four bits = group type code,
	 *              fifth bit = version (0 is A, 1 is B)
	 *              sixth bit -> TP, next five bits -> PTY
	 * If version -> B, 3rd block is also PI and the offset
	 *              word of block 3 changes from C to C';
	 */
	group->blocks[0].infoword = st->pi;
	group->blocks[1].infoword = (code & 0xF) << 12 |
				    (version & 1) << 11 |
				    (st->tp & 1) << 10 |
				    (st->pty & 0x1f) << 5;

	if (version == RDS_GROUP_VERSION_B) {
		group->blocks[2].infoword = st->pi;
		group->blocks[2].offset_word =
			offset_words[RDS_ALT_OFFSET_WORD_C_IDX];
	}

	switch (code) {
	case 0:
		ret = rds_generate_group_0(enc, group, version);
		break;
	case 1:
		ret = rds_generate_group_1(enc, group, version);
		break;
	case 2:
		ret = rds_generate_group_2(enc, group, version);
		break;
	case 4:
		ret = rds_generate_group_4(enc, group, version);
		break;
	case 10:
		ret = rds_generate_group_10(enc, group, version);
		break;
	case 15:
		ret = rds_generate_group_15(enc, group, version);
		break;
	default:
		return -1;
	}

	if (ret < 0)
		return ret;

	return rds_generate_group_samples(group);
}

/*****************\
* GROUP SCHEDULER *
\*****************/

static int
rds_get_next_group(struct rds_encoder *enc, struct rds_group *group)
{
	const struct rds_encoder_state *st = enc->state;
	static int8_t groups_per_sec_counter = 0;
	static uint16_t groups_per_min_counter = 0;
	static uint8_t ptyn_cnt = 0;
	int ret = 0;

	/* Every 1 min send the 4A (CT) group and reset
	 * the counter */
	if (groups_per_min_counter >= RDS_GROUPS_PER_MIN) {
		ret = rds_generate_group(enc, group, 4, RDS_GROUP_VERSION_A);
		if (ret >= 0)
			groups_per_min_counter = 0;
		return ret;
	}
	/* On every second send the PS and the DI one time
	 * (so 4 0A/OB groups). This matches table 4 that
	 * shows the repetition rates of each group
	 * and will also update TA, MS and AF */
	if (groups_per_sec_counter < 4) {
		if (st->ps_set) {
			if (st->af_set)
				ret = rds_generate_group(enc, group, 0,
							 RDS_GROUP_VERSION_A);
			else
				ret = rds_generate_group(enc, group, 0,
							 RDS_GROUP_VERSION_B);
		} else
			ret = rds_generate_group(enc, group, 15,
						 RDS_GROUP_VERSION_B);
	}
	/* Send a 1A group to update ECC / LIC on the receiver */
	else if (groups_per_sec_counter < 5 && (st->ecc_set || st->lic_set)) {
		ret = rds_generate_group(enc, group, 1, RDS_GROUP_VERSION_A);
	}
	/* Send 2 10A groups for PTYN if available */
	else if (groups_per_sec_counter < 7 && st->ptyn_set && ptyn_cnt < 2) {
		ret = rds_generate_group(enc, group, 10, RDS_GROUP_VERSION_A);
		ptyn_cnt++;
	}
	/* On the remaining slots send 2A groups to set
	 * the RT buffer on the receiver */
	else if (groups_per_sec_counter < RDS_GROUPS_PER_SEC && st->rt_set) {
		ret = rds_generate_group(enc, group, 2, RDS_GROUP_VERSION_A);
	} else {
		groups_per_sec_counter = -1;
		ret = rds_get_next_group(enc, group);
	}

	if (ret >= 0) {
		groups_per_sec_counter++;
		groups_per_min_counter++;
	}

	if (ptyn_cnt >= 2)
		ptyn_cnt = 0;

	return ret;
}

/* Ask a group from the scheduler and upsample its
 * waveform to the oscilator's sample rate so that it
 * can be modulated by the 57KHz subcarrier */
static struct rds_upsampled_group *
rds_get_next_upsampled_group(struct rds_encoder *enc)
{
	const struct resampler_data *rsmpl = enc->rsmpl;
	struct rds_group next_group;
	struct rds_upsampled_group *outbuf = NULL;
	int out_idx = 0;
	int ret = 0;

	/* Encoder is inactive or is being terminated
	 * so skip processing */
	if (enc->status != RDS_ENC_ACTIVE)
		return NULL;

	/* Only mess with the unused output buffer */
	out_idx = enc->curr_outbuf_idx == 0 ? 1 : 0;
	outbuf = &enc->outbuf[out_idx];

	/* Update current group */
	ret = rds_get_next_group(enc, &next_group);
	if (unlikely(ret < 0)) {
		outbuf->result = -1;
		goto cleanup;
	}

	/* Resample current group's waveform to the
	 * main oscilators sampling rate */
	outbuf->waveform_samples =
			resampler_upsample_rds(rsmpl,
						next_group.
						samples_buffer,
						outbuf->waveform,
						RDS_GROUP_SAMPLES,
						enc->upsampled_waveform_len);
	if (unlikely(outbuf->waveform_samples < 0)) {
		outbuf->waveform_samples = 0;
		outbuf->result = -2;
		goto cleanup;
	} else
		outbuf->result = 0;

 cleanup:
	return outbuf;
}

static void *
rds_main_loop(void *arg)
{
	struct rds_encoder *enc = (struct rds_encoder *)arg;
	struct rds_upsampled_group *outbuf = NULL;

	while (enc->status == RDS_ENC_ACTIVE) {
		pthread_mutex_lock(&enc->rds_process_mutex);
		while (pthread_cond_wait(&enc->rds_process_trigger,
					 &enc->rds_process_mutex) != 0);

		if (enc->status != RDS_ENC_ACTIVE) {
			pthread_mutex_unlock(&enc->rds_process_mutex);
			break;
		}

		outbuf = rds_get_next_upsampled_group(enc);
		if (outbuf->result < 0) {
			enc->status = RDS_ENC_FAILED;
			utils_err("[RDS] Group generation failed with code: %i\n",
				  outbuf->result);
		}
		pthread_mutex_unlock(&enc->rds_process_mutex);
	}

	rds_encoder_destroy(enc);

	return arg;
}


/*************\
* ENTRY POINT *
\*************/

/* The callback from the main loop to get the
 * next -upsampled- waveform sample */
float
rds_get_next_sample(struct rds_encoder *enc)
{
	const struct rds_upsampled_group *outbuf = &enc->outbuf[enc->curr_outbuf_idx];
	const struct rds_encoder_state *st = enc->state;
	static int samples_out = 0;
	float out = 0;

	/* Encoder is disabled, don't do any processing */
	if (enc->status != RDS_ENC_ACTIVE || !st->enabled)
		return 0;

	/* We have remaining samples from the last group */
	if (samples_out < outbuf->waveform_samples) {
		out = outbuf->waveform[samples_out++];
		return out;
	}

	/* Last group was sent, go for the next one */

	/* Switch to the new output buffer */
	enc->curr_outbuf_idx = enc->curr_outbuf_idx == 0 ? 1 : 0;

	/* Ask for a new group to be generated on the old buffer */
	pthread_cond_signal(&enc->rds_process_trigger);

	/* Reset counter and start consuming the new buffer */
	samples_out = 0;
	outbuf = &enc->outbuf[enc->curr_outbuf_idx];
	out = outbuf->waveform[samples_out++];
	return out;
}

/****************\
* INIT / DESTROY *
\****************/

int
rds_encoder_init(struct rds_encoder *enc, jack_client_t *client,
		 struct resampler_data *rsmpl)
{
	int ret = 0;

	if (enc == NULL)
		return -1;

	memset(enc, 0, sizeof(struct rds_encoder));
	enc->rsmpl = rsmpl;

	enc->status = RDS_ENC_INACTIVE;

	/* Initialize processing lock */
	pthread_mutex_init(&enc->rds_process_mutex, NULL);
	pthread_cond_init(&enc->rds_process_trigger, NULL);

	/* Initialize I/O channel for encoder's state */
	enc->state_map = utils_shm_init(RDS_ENC_SHM_NAME,
					sizeof(struct rds_encoder_state));
	if(!enc->state_map)
		return -2;
	enc->state = (struct rds_encoder_state*) enc->state_map->mem;
	utils_dbg("[RDS] Control channel ready\n");

	/* Allocate buffers */
	enc->upsampled_waveform_len = num_resampled_samples(RDS_SAMPLE_RATE,
							    rsmpl->osc_samplerate,
							    RDS_GROUP_SAMPLES);
	enc->upsampled_waveform_len *= sizeof(float);


	enc->outbuf[0].waveform = (float *) malloc(enc->upsampled_waveform_len);
	if (enc->outbuf[0].waveform == NULL) {
		ret = -3;
		goto cleanup;
	}
	memset(enc->outbuf[0].waveform, 0, enc->upsampled_waveform_len);

	enc->outbuf[1].waveform = (float *) malloc(enc->upsampled_waveform_len);
	if (enc->outbuf[1].waveform == NULL) {
		ret = -4;
		goto cleanup;
	}
	memset(enc->outbuf[1].waveform, 0, enc->upsampled_waveform_len);

	/* Set default state */
	enc->state->ms = RDS_MS_DEFAULT;
	enc->state->di = RDS_DI_STEREO | RDS_DI_DYNPTY;

	/* Let main loop run */
	enc->status = RDS_ENC_ACTIVE;

	/* Create processing thread */
	ret = jack_client_create_thread(client, &enc->tid,
					jack_client_real_time_priority(client),
					jack_is_realtime(client),
					rds_main_loop, (void *)enc);
	if (ret < 0) {
		utils_err("[JACKD] Could not create processing thread\n");
		enc->status = RDS_ENC_FAILED;
		ret = -5;
	}

 cleanup:
	if (ret < 0) {
		utils_err("[RDS] Init failed with code: %i\n", ret);
		rds_encoder_destroy(enc);
	} else
		utils_dbg("[RDS] Init complete\n");

	return ret;
}

void
rds_encoder_destroy(struct rds_encoder *enc)
{
	int error = 0;

	switch(enc->status) {
	case RDS_ENC_INACTIVE:
		goto inactive;
	case RDS_ENC_FAILED:
		error = 1;
		goto inactive;
	case RDS_ENC_TERMINATED:
		return;
	case RDS_ENC_ACTIVE:
	default:
		break;
	}

	utils_dbg("[RDS] Graceful exit\n");

	/* Stop rds main loop and disable the encoder
	 * so that future requests for rds samples are ignored */
	enc->status = RDS_ENC_INACTIVE;

	/* Trigger main loop so that it gets un-stuck and
	 * can properly exit */
	pthread_cond_signal(&enc->rds_process_trigger);
	if(enc->tid)
		pthread_join(enc->tid, NULL);

 inactive:
	enc->status = RDS_ENC_TERMINATED;

	/* Cleanup */
	utils_shm_destroy(enc->state_map, 1);
	enc->state_map = NULL;
	utils_dbg("[RDS] Control channel closed\n");
	pthread_mutex_destroy(&enc->rds_process_mutex);
	pthread_cond_destroy(&enc->rds_process_trigger);

	if (enc->outbuf[0].waveform != NULL) {
		free(enc->outbuf[0].waveform);
		enc->outbuf[0].waveform = NULL;
	} if (enc->outbuf[1].waveform != NULL) {
		free(enc->outbuf[1].waveform);
		enc->outbuf[1].waveform = NULL;
	}

	utils_dbg("[RDS] Destroyed\n");

	/* Signal the parent it's game over, in case we
	 * ended up here due to an error. */
	if (error)
		raise(SIGTERM);
}
