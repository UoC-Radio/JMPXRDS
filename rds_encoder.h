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

#include "resampler.h" /* For resampler types and prototypes */
#include <stdint.h> /* For typed ints */
#include <jack/jack.h>	/* For jack-related types */

/* RDS encoding takes a data stream of specialy formated data,
 * does a differential encoding on it and passes it through a
 * special filter. The filter encodes each data bit as a biphase
 * symbol and passes it through a low-pass filter to produce a
 * "shaped" symbol. The samples generated must then be modulated
 * by the 57KHz subcarrier(the 3rd harmonic of the 19KHz stereo pilot)
 * and added to the MPX signal.
 *
 * RDS data rate is based on a clock obtained by dividing
 * the 57KHz subcarrier by 48. So it's 1187.5Hz (or bits/s).
 * To make calculations easier we use twice that and later
 * divide by 2 when calculating the sample rate */
#define RDS_BASIC_CLOCK_FREQ_x2	2375

/*
 * This implementation is based on the work of Robert Langmeier
 * and his project RdsEnc. For more information visit
 * http://www.langmeier.ch/
 *
 * The idea is that instead of doing all the calculations related
 * to the filter at runtime, a lookup table is used with pre-calculated
 * symbols (waveforms, feel free to plot them :-)). After all it's
 * either 0 or 1, so we know all the possible inputs.
 */
#define RDS_SAMPLES_PER_SYMBOL	40

/* Sample rate of the RDS encoder -> 47500Hz */
#define RDS_SAMPLE_RATE	((RDS_BASIC_CLOCK_FREQ_x2 * RDS_SAMPLES_PER_SYMBOL) / 2)

/*
 * Basic RDS data format elements (section 5.1)
 * The data transmitted is split in blocks, each block has an infoword
 * and a checkword (for error correction). Each data packet is called
 * a group and it contains 4 blocks. */
#define RDS_INFOWORD_SIZE_BITS	16
#define RDS_CHECKWORD_SIZE_BITS	10
#define RDS_BLOCK_SIZE_BITS	26
#define RDS_BLOCK_SAMPLES	(RDS_BLOCK_SIZE_BITS * RDS_SAMPLES_PER_SYMBOL)

#define RDS_BLOCKS_PER_GROUP	4
#define RDS_GROUP_SIZE_BITS	(RDS_BLOCK_SIZE_BITS * RDS_BLOCKS_PER_GROUP)
#define	RDS_GROUP_SAMPLES	(RDS_GROUP_SIZE_BITS * RDS_SAMPLES_PER_SYMBOL)

/*
 * It takes approximately 87.6ms to transmit a group so we can transmit
 * almost 12 groups a second. That means we transmit almost 12 * 60 groups
 * per min.
 */
#define RDS_GROUPS_PER_SEC	12
#define	RDS_GROUPS_PER_MIN	(RDS_GROUPS_PER_SEC * 60)

struct rds_block {
	uint16_t infoword;
	uint16_t offset_word:10;
	uint16_t checkword:10;
};

struct rds_group {
	int code;
	int version;
	struct rds_block blocks[RDS_BLOCKS_PER_GROUP];
	uint16_t last_sample_idx;
	float samples_buffer[RDS_GROUP_SAMPLES];
};

#define RDS_GROUP_VERSION_A	0
#define	RDS_GROUP_VERSION_B	1
#define RDS_GROUP_VERSION_MAX	RDS_GROUP_VERSION_B

struct rds_upsampled_group {
	float *waveform;
	int waveform_samples;
	int result;
};

#define RDS_PS_LENGTH	8
#define	RDS_PTYN_LENGTH	8
#define	RDS_RT_LENGTH	64

struct rds_encoder_state {
	uint16_t pi;
	uint8_t	ecc;
	uint8_t ecc_set:1;
	uint16_t lic:12;
	uint8_t lic_set:1;
	uint8_t	pty:5;
	uint8_t	ta:1;
	uint8_t	tp:1;
	uint8_t	ms:1;
	uint8_t	di:4;
	char ps[RDS_PS_LENGTH];
	uint8_t ps_set:1;
	uint8_t	ps_idx:2;
	char ptyn[RDS_PTYN_LENGTH];
	uint8_t	ptyn_set:1;
	uint8_t ptyn_idx:1;
	uint8_t	ptyn_flush:1;
	char rt[RDS_RT_LENGTH];
	uint8_t	rt_set:1;
	uint8_t rt_idx:4;
	uint8_t	rt_segments:4;
	uint8_t	rt_flush:1;
	uint8_t	af_data[12];
	uint8_t	af_set:1;
	uint8_t	af_len:4;
	uint8_t	af_idx:4;
};

struct rds_encoder {
	struct rds_encoder_state *state;
	struct resampler_data *rsmpl;
	struct rds_upsampled_group outbuf[2];
	int curr_outbuf_idx;
	size_t upsampled_waveform_len;
	jack_client_t *fmmod_client;
};

#define	RDS_MS_SPEECH	0
#define	RDS_MS_MUSIC	1
#define	RDS_MS_DEFAULT	RDS_MS_MUSIC

#define	RDS_DI_STEREO		0x1
#define	RDS_DI_ARTIFICIAL_HEAD	0x2
#define	RDS_DI_COMPRESSED	0x4
#define	RDS_DI_DYNPTY		0x8

/* Radio Text special characters
 * (Section 6.1.5.3) */
#define RDS_RT_CR		0x0D	/* Carriage return - end of message */
#define RDS_RT_LF		0x0A	/* Line feed */
#define RDS_RT_END_OF_HEADLINE	0x0B	/* End of headline indicator */
#define RDS_RT_SOFT_HYPHEN	0x1F	/* Split a word to the next line if needed */

/* Prototypes */
int rds_encoder_init(struct rds_encoder *enc, struct resampler_data *rsmpl);
void rds_encoder_destroy(struct rds_encoder *enc);
float rds_get_next_sample(struct rds_encoder *enc);

/* Name of shared struct rds_encoder to be used between
 * the main process and the client */
#define RDS_ENC_SHM_NAME	"RDS_ENC_SHM"

/* Getters/Setters */
uint16_t rds_get_pi(struct rds_encoder_state *st);
int rds_set_pi(struct rds_encoder_state *st, uint16_t pi);
uint8_t rds_get_ecc(struct rds_encoder_state *st);
int rds_set_ecc(struct rds_encoder_state *st, uint8_t ecc);
uint16_t rds_get_lic(struct rds_encoder_state *st);
int rds_set_lic(struct rds_encoder_state *st, uint16_t lic);
uint8_t rds_get_pty(struct rds_encoder_state *st);
int rds_set_pty(struct rds_encoder_state *st, uint8_t pty);
uint8_t rds_get_ta(struct rds_encoder_state *st);
int rds_set_ta(struct rds_encoder_state *st, uint8_t ta);
uint8_t rds_get_ms(struct rds_encoder_state *st);
int rds_set_ms(struct rds_encoder_state *st, uint8_t ms);
uint8_t rds_get_di(struct rds_encoder_state *st);
int rds_set_di(struct rds_encoder_state *st, uint8_t di);
char* rds_get_ps(struct rds_encoder_state *st);
int rds_set_ps(struct rds_encoder_state *st, const char* ps);
char* rds_get_ptyn(struct rds_encoder_state *st);
int rds_set_ptyn(struct rds_encoder_state *st, const char* ptyn);
char* rds_get_rt(struct rds_encoder_state *st);
int rds_set_rt(struct rds_encoder_state *st, const char* rt, int flush);
