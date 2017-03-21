/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - General utilities
 *
 * Copyright (C) 2016 Nick Kossifidis <mickflemm@gmail.com>
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

/************************\
* SHARED MEMORY HANDLING *
\************************/

#define FMMOD_CTL_SHM_NAME	"/FMMOD_CTL_SHM"
#define RDS_ENC_SHM_NAME	"/RDS_ENC_SHM"
#define RTP_SRV_SHM_NAME	"/RTP_SRV_SHM"

struct shm_mapping {
	const char* name;
	int size;
	int fd;
	void* mem;
};

struct shm_mapping*
utils_shm_init(const char* name, int size);

struct shm_mapping*
utils_shm_attach(const char* name, int size);

void
utils_shm_destroy(struct shm_mapping* shmem, int unlink);

void
utils_shm_unlink_all();

/****************\
* CONSOLE OUTPUT *
\****************/

void
utils_ann(const char* msg);

void
utils_info(const char* fmt,...);

void
utils_err(const char* fmt,...);

void
utils_perr(const char* msg);

void
utils_dbg(const char* fmt,...);

void
utils_trace(const char* fmt,...);

