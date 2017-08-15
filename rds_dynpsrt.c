/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RDS Dynamic PSN / RT handling
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
#include "utils.h"
#include <stdio.h>		/* For fopen(), FILE etc */
#include <string.h>		/* For memset/strnlen/strncpy etc */
#include <unistd.h>		/* For read() / close() */
#include <pthread.h>		/* For pthread support */
#include <time.h>		/* For clock_gettime() */

/*********\
* HELPERS *
\*********/

static void
rds_dynpsrt_cond_sleep(pthread_cond_t *trig, pthread_mutex_t *mutex, int delay_secs)
{
	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += delay_secs;
	pthread_mutex_lock(mutex);
	while(!pthread_cond_timedwait(trig, mutex, &ts));
	pthread_mutex_unlock(mutex);
}

/*************\
* DYNAMIC PSN *
\*************/

/*
 * Note: Dynamic PSN is highly discouraged and RDS forum has issued various
 * statements against it. I've put it here since many stations use it and I
 * got a request from radio Best 94.7 in Heraklion for it. This operation will
 * switch the station's name every DYNPS_DELAY_SECS so that the PSN field on
 * car / old radios shows like a scrolling text. The station's name (fixed_ps)
 * will be shown each time the dynamic PSN text has been fully "scrolled".
 * There are various dynamic PSN modes available out there, here I've implemented
 * the "scroll by 8 characters" mode since it's the most reliable and takes fewer
 * time to scroll the full text.
 */

static void
rds_dynps_sanitize(struct rds_dynps_state *dps)
{
	/* TODO: Maybe clean up symbols, dots etc */
	dps->no_segments = (strnlen(dps->string, DYNPS_MAX_CHARS)  + 7) / RDS_PS_LENGTH;
	return;
}

static char*
rds_dynps_get_next_str_segment(struct rds_dynps_state *dps)
{
	static char segment[RDS_PS_LENGTH + 1] = {0};

	if(dps->no_segments == 0)
		return dps->fixed_ps;

	if(dps->curr_segment + 1 > dps->no_segments) {
		dps->curr_segment = 0;
		return dps->fixed_ps;
	}

	strncpy(segment, dps->string + (dps->curr_segment * RDS_PS_LENGTH),
		RDS_PS_LENGTH);
	dps->curr_segment++;

	return segment;
}

static void
rds_dynps_sleep(struct rds_dynps_state *dps)
{
	rds_dynpsrt_cond_sleep(&dps->sleep_trig, &dps->sleep_mutex, DYNPS_DELAY_SECS);
}

static void
rds_dynps_update(struct rds_dynps_state *dps, const char* segment)
{
	struct rds_encoder_state *st = dps->st;
	int ret = 0;
	ret = rds_set_ps(st, segment);
	utils_dbg("[DYNPS] %s, status: %i\n",segment, ret);
}

static void*
rds_dynps_consumer_thread(void *arg)
{
	struct rds_dynps_state *dps = (struct rds_dynps_state *) arg;
	char* segment = NULL;

	while(dps->active) {
		pthread_mutex_lock(&dps->dynps_proc_mutex);
		segment = rds_dynps_get_next_str_segment(dps);
		rds_dynps_update(dps, segment);
		pthread_mutex_unlock(&dps->dynps_proc_mutex);
		rds_dynps_sleep(dps);
	}

	return arg;
}

static void*
rds_dynps_filemon_thread(void *arg)
{
	struct rds_dynps_state *dps = (struct rds_dynps_state *) arg;
	char *res = NULL;
	int ret = 0;
	FILE *file = NULL;

	while(dps->active) {
		/* Blocking read until we get an event */
		if(dps->opened)
			ret = read(dps->inotify_fd, dps->event_buf, EVENT_LEN);
		if(ret < 0) {
			utils_perr("[DYNPS] Failed to read inotify fd, read()");
			continue;
		}

		file = fopen(dps->filepath, "r");
		if(!file) {
			utils_perr("[DYNPS] Failed to open %s for reading PS, fopen()");
			continue;
		}

		dps->opened = 1;

		pthread_mutex_lock(&dps->dynps_proc_mutex);
		res = fgets(dps->string, DYNPS_MAX_CHARS - 1, file);
		if(!res) {
			utils_perr("[DYNPS] Failed to get string from file, fgets()");
			continue;
		}
		rds_dynps_sanitize(dps);
		dps->curr_segment = 0;
		pthread_mutex_unlock(&dps->dynps_proc_mutex);
		fclose(file);
	}

	return arg;
}

void
rds_dynps_destroy(struct rds_dynps_state *dps)
{
	struct rds_encoder_state *st = dps->st;
	dps->active = 0;
	pthread_mutex_lock(&dps->sleep_mutex);
	pthread_cond_signal(&dps->sleep_trig);
	pthread_mutex_unlock(&dps->sleep_mutex);
	if(dps->inotify_fd && dps->watch_fd)
		inotify_rm_watch(dps->inotify_fd, dps->watch_fd);
	if(dps->inotify_fd)
		close(dps->inotify_fd);
	if(dps->dynps_filemon_tid)
		pthread_cancel(dps->dynps_filemon_tid);
	if(dps->dynps_consumer_tid)
		pthread_cancel(dps->dynps_consumer_tid);
	if(dps->fixed_ps)
		rds_set_ps(st, dps->fixed_ps);
	pthread_mutex_destroy(&dps->dynps_proc_mutex);
	pthread_mutex_destroy(&dps->sleep_mutex);
	pthread_cond_destroy(&dps->sleep_trig);
}

int
rds_dynps_init(struct rds_dynps_state *dps, struct rds_encoder_state *st, const char* filepath)
{
	int ret = 0;

	if(!st->ps_set) {
		utils_wrn("[DYNPS] Fixed PS not set, dynamic PS request ignored\n");
		return -1;
	}

	memset(dps, 0, sizeof(struct rds_dynps_state));

	dps->st = st;
	pthread_mutex_init(&dps->dynps_proc_mutex, NULL);
	pthread_mutex_init(&dps->sleep_mutex, NULL);
	pthread_cond_init(&dps->sleep_trig, NULL);

	strncpy(dps->fixed_ps, st->ps, RDS_PS_LENGTH);

	dps->inotify_fd = inotify_init();
	if(dps->inotify_fd < 0) {
		utils_perr("[DYNPS] Unable to initialize inotify, inotify_init()");
		ret = -2;
		goto cleanup;
	}

	dps->filepath = filepath;
	dps->watch_fd = inotify_add_watch(dps->inotify_fd, dps->filepath, IN_MODIFY);
	if(dps->watch_fd < 0) {
		utils_perr("[DYNPS] Unable to add inotify watch, inotify_add_watch()");
		ret = -3;
		goto cleanup;
	}

	dps->active = 1;
	ret = pthread_create(&dps->dynps_filemon_tid, NULL,
			     rds_dynps_filemon_thread, (void*) dps);
	if(ret != 0) {
		utils_err("[DYNPS] Unable to create file monitor thread, pthred_create(): %d", ret);
		ret = -4;
		goto cleanup;
	}

	ret = pthread_create(&dps->dynps_consumer_tid, NULL,
			     rds_dynps_consumer_thread, (void*) dps);
	if(ret != 0) {
		utils_err("[DYNPS] Unable to create file monitor thread, pthred_create(): %d", ret);
		ret = -4;
		goto cleanup;
	}

	return ret;

 cleanup:
	rds_dynps_destroy(dps);
	return ret;
}


/*******************\
* DYNAMIC RadioText *
\*******************/

static void
rds_dynrt_sanitize(struct rds_dynrt_state *drt)
{
	/* TODO: Maybe clean up symbols, dots etc */
	drt->no_segments = (strnlen(drt->string, DYNRT_MAX_CHARS)  + 7) / RDS_RT_LENGTH;
	return;
}

static char*
rds_dynrt_get_next_str_segment(struct rds_dynrt_state *drt)
{
	static char segment[RDS_RT_LENGTH + 1] = {0};

	if(drt->curr_segment + 1 > drt->no_segments)
		drt->curr_segment = 0;

	strncpy(segment, drt->string + (drt->curr_segment * RDS_RT_LENGTH),
		RDS_RT_LENGTH);
	drt->curr_segment++;

	return segment;
}

static void
rds_dynrt_sleep(struct rds_dynrt_state *drt)
{
	rds_dynpsrt_cond_sleep(&drt->sleep_trig, &drt->sleep_mutex, DYNRT_DELAY_SECS);
}

static void
rds_dynrt_update(struct rds_dynrt_state *drt, const char* segment)
{
	struct rds_encoder_state *st = drt->st;
	int ret = 0;
	ret = rds_set_rt(st, segment, 1);
	utils_dbg("[DYNRT] %s, status: %i\n",segment, ret);
}

static void*
rds_dynrt_consumer_thread(void *arg)
{
	struct rds_dynrt_state *drt = (struct rds_dynrt_state *) arg;
	char* segment = NULL;

	while(drt->active) {
		pthread_mutex_lock(&drt->dynrt_proc_mutex);
		segment = rds_dynrt_get_next_str_segment(drt);
		rds_dynrt_update(drt, segment);
		pthread_mutex_unlock(&drt->dynrt_proc_mutex);
		rds_dynrt_sleep(drt);
	}

	return arg;
}

static void*
rds_dynrt_filemon_thread(void *arg)
{
	struct rds_dynrt_state *drt = (struct rds_dynrt_state *) arg;
	char *res = NULL;
	int ret = 0;
	FILE *file = NULL;

	while(drt->active) {
		/* Blocking read until we get an event */
		if(drt->opened)
			ret = read(drt->inotify_fd, drt->event_buf, EVENT_LEN);
		if(ret < 0) {
			utils_perr("[DYNRT] Failed to read inotify fd, read()");
			continue;
		}

		file = fopen(drt->filepath, "r");
		if(!file) {
			utils_perr("[DYNRT] Failed to open %s for reading PS, fopen()");
			continue;
		}

		drt->opened = 1;

		pthread_mutex_lock(&drt->dynrt_proc_mutex);
		res = fgets(drt->string, DYNRT_MAX_CHARS - 1, file);
		if(!res) {
			utils_perr("[DYNRT] Failed to get string from file, fgets()");
			continue;
		}
		rds_dynrt_sanitize(drt);
		drt->curr_segment = 0;
		pthread_mutex_unlock(&drt->dynrt_proc_mutex);
		fclose(file);
	}

	return arg;
}

void
rds_dynrt_destroy(struct rds_dynrt_state *drt)
{
	struct rds_encoder_state *st = drt->st;
	drt->active = 0;
	pthread_mutex_lock(&drt->sleep_mutex);
	pthread_cond_signal(&drt->sleep_trig);
	pthread_mutex_unlock(&drt->sleep_mutex);
	if(drt->inotify_fd && drt->watch_fd)
		inotify_rm_watch(drt->inotify_fd, drt->watch_fd);
	if(drt->inotify_fd)
		close(drt->inotify_fd);
	if(drt->dynrt_filemon_tid)
		pthread_cancel(drt->dynrt_filemon_tid);
	if(drt->dynrt_consumer_tid)
		pthread_cancel(drt->dynrt_consumer_tid);
	if(drt->fixed_rt)
		rds_set_rt(st, drt->fixed_rt, 1);
	pthread_mutex_destroy(&drt->dynrt_proc_mutex);
	pthread_mutex_destroy(&drt->sleep_mutex);
	pthread_cond_destroy(&drt->sleep_trig);
}

int
rds_dynrt_init(struct rds_dynrt_state *drt, struct rds_encoder_state *st, const char* filepath)
{
	int ret = 0;

	if(!st->rt_set) {
		utils_wrn("[DYNRT] Fixed RT not set, dynamic RT request ignored\n");
		return -1;
	}

	memset(drt, 0, sizeof(struct rds_dynrt_state));

	drt->st = st;
	pthread_mutex_init(&drt->dynrt_proc_mutex, NULL);
	pthread_mutex_init(&drt->sleep_mutex, NULL);
	pthread_cond_init(&drt->sleep_trig, NULL);

	strncpy(drt->fixed_rt, st->rt, RDS_RT_LENGTH);

	drt->inotify_fd = inotify_init();
	if(drt->inotify_fd < 0) {
		utils_perr("[DYNRT] Unable to initialize inotify, inotify_init()");
		ret = -2;
		goto cleanup;
	}

	drt->filepath = filepath;
	drt->watch_fd = inotify_add_watch(drt->inotify_fd, drt->filepath, IN_MODIFY);
	if(drt->watch_fd < 0) {
		utils_perr("[DYNRT] Unable to add inotify watch, inotify_add_watch()");
		ret = -3;
		goto cleanup;
	}

	drt->active = 1;
	ret = pthread_create(&drt->dynrt_filemon_tid, NULL,
			     rds_dynrt_filemon_thread, (void*) drt);
	if(ret != 0) {
		utils_err("[DYNRT] Unable to create file monitor thread, pthred_create(): %d", ret);
		ret = -4;
		goto cleanup;
	}

	ret = pthread_create(&drt->dynrt_consumer_tid, NULL,
			     rds_dynrt_consumer_thread, (void*) drt);
	if(ret != 0) {
		utils_err("[DYNRT] Unable to create file monitor thread, pthred_create(): %d", ret);
		ret = -4;
		goto cleanup;
	}

	return ret;

 cleanup:
	rds_dynrt_destroy(drt);
	return ret;
}
