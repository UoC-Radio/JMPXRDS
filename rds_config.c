/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RDS Configuration helpers
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

#include "rds_encoder.h"
#include <string.h> /* For memset/memcpy/strnlen */

uint16_t
rds_get_pi(struct rds_encoder *enc)
{
	return enc->pi;
}

int
rds_set_pi(struct rds_encoder *enc, uint16_t pi)
{
	enc->pi = pi;
	return 0;
}

uint8_t
rds_get_ecc(struct rds_encoder *enc)
{
	return enc->ecc;
}

int
rds_set_ecc(struct rds_encoder *enc, uint8_t ecc)
{
	enc->ecc = ecc;
	if(enc->ecc == 0)
		enc->ecc_set = 0;
	else
		enc->ecc_set = 1;
	return 0;
}

uint16_t
rds_get_lic(struct rds_encoder *enc)
{
	return enc->lic;
}

int
rds_set_lic(struct rds_encoder *enc, uint16_t lic)
{
	enc->lic = lic & 0xFFF;
	if(enc->lic == 0)
		enc->lic_set = 0;
	else
		enc->lic_set = 1;
	return 0;
}

uint8_t
rds_get_pty(struct rds_encoder *enc)
{
	return enc->pty;
}

int
rds_set_pty(struct rds_encoder *enc, uint8_t pty)
{
	if(pty <= 31)
		enc->pty = pty;
	else
		return -1;
	return 0;
}

uint8_t
rds_get_ta(struct rds_encoder *enc)
{
	return enc->ta;
}

int
rds_set_ta(struct rds_encoder *enc, uint8_t ta)
{
	enc->ta = ta & 1;
	return 0;
}

uint8_t
rds_get_ms(struct rds_encoder *enc)
{
	return enc->ms;
}

int
rds_set_ms(struct rds_encoder *enc, uint8_t ms)
{
	enc->ms = ms & 1;
	return 0;
}

uint8_t
rds_get_di(struct rds_encoder *enc)
{
	return enc->di;
}

int
rds_set_di(struct rds_encoder *enc, uint8_t di)
{
	enc->di = di & 0xF;
	return 0;
}

char*
rds_get_ps(struct rds_encoder *enc)
{
	static char ps[RDS_PS_LENGTH + 1];
	memset(ps, 0, RDS_PS_LENGTH + 1);
	memcpy(ps, enc->ps, RDS_PS_LENGTH);
	return ps;
}

int
rds_set_ps(struct rds_encoder *enc, const char* ps)
{
	int pslen = 0;
	int i = 0;

	if(enc == NULL || ps == NULL)
		return -1;

	pslen = strnlen(ps, RDS_PS_LENGTH);
	if(pslen > RDS_PS_LENGTH)
		return -1;

	memset(enc->ps, 0, RDS_PS_LENGTH);

	if(pslen == 0) {
		enc->ps_set = 0;
		return 0;
	}

	for(i = 0; i < pslen; i++) {
		if((ps[i] >= 0x20) ||
		(ps[i] == 0x7F))
			enc->ps[i] = ps[i];
		else
			enc->ps[i] = 0;
	}
	enc->ps_set = 1;
	enc->ps_idx = 0;

	return 0;
}

char*
rds_get_ptyn(struct rds_encoder *enc)
{
	static char ptyn[RDS_PTYN_LENGTH + 1];
	if(!enc->ptyn_set)
		return NULL;

	memset(ptyn, 0, RDS_PTYN_LENGTH + 1);
	memcpy(ptyn, enc->ptyn,RDS_PTYN_LENGTH);

	return ptyn;
}

int
rds_set_ptyn(struct rds_encoder *enc, const char* ptyn)
{
	int ptynlen = 0;
	int i = 0;

	if(enc == NULL || ptyn == NULL)
		return -1;

	ptynlen = strnlen(ptyn, RDS_PTYN_LENGTH);
	if(ptynlen > RDS_PTYN_LENGTH)
		return -1;

	if(ptynlen == 0) {
		enc->ptyn_set = 0;
		memset(enc->ptyn, 0, RDS_PTYN_LENGTH);
		return 0;
	}

	/* Flip A/B flag to flush PTYN buffer
	 * on receiver */
	if(enc->ptyn_set)
		enc->ptyn_flush = enc->ptyn_flush ? 0 : 1;

	memset(enc->ptyn, 0, RDS_PTYN_LENGTH);
	for(i = 0; i < ptynlen; i++) {
		if((ptyn[i] >= 0x20) ||
		(ptyn[i] == 0x7F))
			enc->ptyn[i] = ptyn[i];
		else
			enc->ptyn[i] = 0;
	}
	enc->ptyn_set = 1;
	enc->ptyn_idx = 0;

	return 0;
}

char*
rds_get_rt(struct rds_encoder *enc)
{
	static char rt[RDS_RT_LENGTH + 1];

	if(!enc->rt_set)
		return NULL;

	memset(rt, 0, RDS_RT_LENGTH + 1);
	memcpy(rt, enc->rt, RDS_RT_LENGTH);

	return rt;
}

int
rds_set_rt(struct rds_encoder *enc, const char* rt, int flush)
{
	int rtlen = 0;
	int i = 0;

	if(enc == NULL || rt == NULL)
		return -1;

	rtlen = strnlen(rt, RDS_RT_LENGTH);
	if(rtlen > RDS_RT_LENGTH)
		return -1;

	if(rtlen == 0) {
		enc->rt_set = 0;
		memset(enc->rt, 0, RDS_RT_LENGTH);
		return 0;
	}

	/* Flip A/B flag to flush RT buffer
	 * on receiver */
	if(flush && enc->rt_set)
		enc->rt_flush = enc->rt_flush ? 0 : 1;

	memset(enc->rt, 0, RDS_RT_LENGTH);
	for(i = 0; i < rtlen; i++) {
		if(((rt[i] < 0x20) &&
		((rt[i] != RDS_RT_CR) ||
		(rt[i] != RDS_RT_LF) ||
		(rt[i] != RDS_RT_END_OF_HEADLINE) ||
		(rt[i] != RDS_RT_SOFT_HYPHEN))) ||
		(rt[i] == 0x7F))
			enc->rt[i] = 0;
		else
			enc->rt[i] = rt[i];
	}

	while(i % 4 && i < RDS_RT_LENGTH - 1) {
		enc->rt[i] = 0x20;
		i++;
	}

	/* According to the standard the RT message
	 * should end with a CR character but some
	 * receivers display a '-' instead
	enc->rt[i - 1] = RDS_RT_CR;
	*/

	enc->rt_segments = i / 4;

	enc->rt_set = 1;
	enc->rt_idx = 0;

	return 0;
}
