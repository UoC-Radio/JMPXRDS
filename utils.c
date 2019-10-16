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

#include "utils.h"
#include <stdlib.h>	/* For malloc(), NULL etc */
#include <string.h>	/* For memset() */
#include <sys/mman.h>	/* For shm_open, mmap etc  */
#include <sys/stat.h>	/* For mode constants */
#include <fcntl.h>	/* For O_* constants */
#include <unistd.h>	/* For ftruncate() */
#include <stdarg.h>	/* For variable argument handling */
#include <stdio.h>	/* For v/printf() */
#include <errno.h>	/* For errno */

/************************\
* SHARED MEMORY HANDLING *
\************************/

struct shm_mapping*
utils_shm_init(const char* name, int size)
{
	int ret = 0;
	struct shm_mapping* shmem = NULL;

	shmem = (struct shm_mapping*)
		malloc(sizeof(struct shm_mapping));
	if(!shmem)
		return NULL;
	memset(shmem, 0, sizeof(struct shm_mapping));
	shmem->name = name;
	shmem->size = size;

	/* Create the shm segment */
	shmem->fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (shmem->fd < 0)
		goto cleanup;

	/* Resize it */
	ret = ftruncate(shmem->fd, size);
	if (ret < 0)
		goto cleanup;

	/* mmap() it */
	shmem->mem = mmap(0, size, PROT_READ | PROT_WRITE,
			  MAP_SHARED, shmem->fd, 0);

 cleanup:
	if(shmem->fd >= 0)
		close(shmem->fd);
	else
		return NULL;

	if (shmem->mem == MAP_FAILED) {
		shm_unlink(name);
		free(shmem);
		shmem = NULL;
	} else if (shmem->mem)
		memset(shmem->mem, 0, shmem->size);

	return shmem;
}

struct shm_mapping*
utils_shm_attach(const char* name, int size)
{
	struct shm_mapping* shmem = NULL;

	shmem = (struct shm_mapping*)
		malloc(sizeof(struct shm_mapping));
	if(!shmem)
		return NULL;
	memset(shmem, 0, sizeof(struct shm_mapping));
	shmem->name = name;
	shmem->size = size;

	/* Open the shm segment */
	shmem->fd = shm_open(name, O_RDWR, 0600);
	if (shmem->fd < 0)
		goto cleanup;

	/* mmap() it */
	shmem->mem = mmap(0, size, PROT_READ | PROT_WRITE,
			  MAP_SHARED, shmem->fd, 0);

 cleanup:
	if(shmem->fd >= 0)
		close(shmem->fd);
	else {
		free(shmem);
		shmem = NULL;
	}

	if (shmem && shmem->mem == MAP_FAILED) {
		shm_unlink(name);
		free(shmem);
		shmem = NULL;
	}

	return shmem;
}

void
utils_shm_destroy(struct shm_mapping* shmem, int unlink)
{
	if(!shmem)
		return;

	munmap(shmem->mem, shmem->size);

	if(unlink)
		shm_unlink(shmem->name);

	free(shmem);
}

void
utils_shm_unlink_all()
{
	shm_unlink(FMMOD_CTL_SHM_NAME);
	shm_unlink(RDS_ENC_SHM_NAME);
	shm_unlink(RTP_SRV_SHM_NAME);
}


/****************\
* CONSOLE OUTPUT *
\****************/

/* Some codes for prety output on the terminal */
#define NORMAL	"\x1B[0m"
#define	BRIGHT	"\x1B[1m"
#define	DIM	"\x1B[2m"
#define RED	"\x1B[31m"
#define GREEN	"\x1B[32m"
#define YELLOW	"\x1B[33m"
#define BLUE	"\x1B[34m"
#define MAGENTA	"\x1B[35m"
#define CYAN	"\x1B[36m"
#define WHITE	"\x1B[37m"

void
utils_ann(const char* msg)
{
	printf(GREEN);
	printf("%s", msg);
	printf(NORMAL);
}

void
utils_info(const char* fmt,...)
{
	va_list args;

	printf(CYAN);
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf(NORMAL);
}

void
utils_wrn(const char* fmt,...)
{
	va_list args;

	fprintf(stderr, YELLOW);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, NORMAL);
}

void
utils_err(const char* fmt,...)
{
	va_list args;

	fprintf(stderr, RED);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, NORMAL);
}

void
utils_perr(const char* msg)
{
	fprintf(stderr, RED);
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	fprintf(stderr, NORMAL);
}

#ifdef DEBUG
void
utils_dbg(const char* fmt,...)
{
	va_list args;

	fprintf(stderr, MAGENTA);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, NORMAL);
}

void
utils_trace(const char* fmt,...)
{
	va_list args;

	fprintf(stderr, YELLOW);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, NORMAL);
}
#else
void
utils_dbg(__attribute__((unused)) const char* fmt,...) {}
void
utils_trace(__attribute__((unused)) const char* fmt,...) {}
#endif
