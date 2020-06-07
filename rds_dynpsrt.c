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
#include <ctype.h>		/* For isspace() */
#include <errno.h>		/* For errno */
#include <stdlib.h>		/* For free() */

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

static int
rds_string_sanitize(char *string, size_t max_len)
{
	/* TODO: Maybe clean up symbols, dots etc */
	int len = strnlen(string, max_len);
	int i = 1;
	int off = 0;

	/* Sanity check */
	if(len <= 0)
		return -1;

	/* Trim trailing white space */
	while(isspace(string[len - i]) && i <= len) {
		string[len - i] = '\0';
		i++;
	}

	/* Only got whitespace ? */
	if(i == len)
		return -2;

	/* Trim leading white space */
	while(isspace(string[off]) && off < len - 1)
		off++;

	/* Move the string in place */
	if(off > 0) {
		memmove(string, string + off, len - off);
		/* In case we didn't have any trailing white space
		 * add a null terminator. */
		if(!isspace(string[len - 1]))
			string[len - off - 1] = '\0';
	}

	/* Update len */
	len = strnlen(string, max_len);
	return len;
}

/*************\
* DYNAMIC PSN *
\*************/

/*
 * Note: Dynamic PSN is highly discouraged and RDS forum has issued various
 * statements against it. I've put it here since many stations use it and I
 * got a request from radio Best 94.7 in Heraklion for it. This operation will
 * switch the station's name every DYNPS_DELAY_SECS so that the PSN field on
 * car / old radios presents a scrolling text. The station's name (fixed_ps)
 * will be shown each time the dynamic PSN text has been fully "scrolled".
 * There are various dynamic PSN modes available out there, here I've implemented
 * the "scroll by 8 characters" mode since it's the most reliable and takes fewer
 * time to scroll the full text.
 */

static void*
rds_dynps_consumer_thread(void *arg)
{
	struct rds_dynps_state *dps = (struct rds_dynps_state *) arg;
	char segment[RDS_PS_LENGTH] = {0};
	int segment_len = 0;
	int off = 0;
	int ret = 0;

	while(dps->active) {
		pthread_mutex_lock(&dps->dynps_proc_mutex);
		if(dps->string_len) {
			if(dps->remaining_len == 0) {
				ret = rds_set_ps(dps->st, dps->fixed_ps);
				utils_dbg("[DYNPS] %s, status: %i\n",dps->fixed_ps, ret);
				dps->remaining_len = dps->string_len;
				pthread_mutex_unlock(&dps->dynps_proc_mutex);
				continue;
			}

			off = dps->string_len - dps->remaining_len;
			segment_len = (dps->remaining_len >= RDS_PS_LENGTH) ?
				      RDS_PS_LENGTH : dps->remaining_len;
			memset(segment, 0, RDS_PS_LENGTH);
			memcpy(segment, dps->string + off, segment_len);

			ret = rds_set_ps(dps->st, segment);
			utils_dbg("[DYNPS] %s, status: %i\n",segment, ret);

			if(dps->remaining_len >= RDS_PS_LENGTH)
				dps->remaining_len -= RDS_PS_LENGTH;
			else
				dps->remaining_len = 0;
		}
		pthread_mutex_unlock(&dps->dynps_proc_mutex);
		rds_dynpsrt_cond_sleep(&dps->sleep_trig, &dps->sleep_mutex,
				       DYNPS_DELAY_SECS);
	}

	utils_dbg("[DYNPS] Consumer terminated\n");
	return arg;
}

static void*
rds_dynps_filemon_thread(void *arg)
{
	struct rds_dynps_state *dps = (struct rds_dynps_state *) arg;
	const struct inotify_event *event = (struct inotify_event*) dps->event_buf;
	const char *res = NULL;
	int ret = 0;
	FILE *file = NULL;

	while(dps->active) {
		/* Blocking read until we get an event */
		ret = read(dps->inotify_fd, dps->event_buf, EVENT_LEN);
		if(ret < 0) {
			if (errno == EINTR)
				continue;
			utils_perr("[DYNPS] Failed to read inotify fd, read()");
			continue;
		}

		utils_dbg("[DYNPS] filemon unblocked\n");

		/* Got an ignore event, terminate */
		if(event->mask & IN_IGNORED)
			break;

		file = fopen(dps->filepath, "r");
		if(!file) {
			utils_perr("[DYNPS] Failed to open %s for reading PS, fopen()");
			continue;
		}

		pthread_mutex_lock(&dps->dynps_proc_mutex);
		memset(dps->string, 0, DYNPS_MAX_CHARS);
		res = fgets(dps->string, DYNPS_MAX_CHARS, file);
		if(!res) {
			utils_perr("[DYNPS] Failed to get string from file, fgets()");
			dps->string_len = 0;
		} else {
			ret = rds_string_sanitize(dps->string, DYNPS_MAX_CHARS);
			if(ret > 0) {
				dps->string_len = ret;
				dps->remaining_len = ret;
			} else
				dps->string_len = 0;
		}
		pthread_mutex_unlock(&dps->dynps_proc_mutex);
		fclose(file);
	}

	utils_dbg("[DYNPS] Filemon terminated\n");
	return arg;
}

void
rds_dynps_destroy(struct rds_dynps_state *dps)
{
	utils_dbg("[DYNPS] Graceful exit\n");

	dps->active = 0;

	pthread_mutex_lock(&dps->sleep_mutex);
	pthread_cond_signal(&dps->sleep_trig);
	pthread_mutex_unlock(&dps->sleep_mutex);

	if(dps->dynps_consumer_tid)
		pthread_join(dps->dynps_consumer_tid, NULL);

	if(dps->inotify_fd && dps->watch_fd)
		inotify_rm_watch(dps->inotify_fd, dps->watch_fd);

	if(dps->inotify_fd)
		close(dps->inotify_fd);

	if(dps->dynps_filemon_tid)
		pthread_join(dps->dynps_filemon_tid, NULL);

	rds_set_ps(dps->st, dps->fixed_ps);

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
	dps->watch_fd = inotify_add_watch(dps->inotify_fd, dps->filepath, IN_MODIFY | IN_IGNORED);
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
		ret = -5;
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

static void*
rds_dynrt_consumer_thread(void *arg)
{
	struct rds_dynrt_state *drt = (struct rds_dynrt_state *) arg;
	int ret = 0;

	while(drt->active) {
		pthread_mutex_lock(&drt->dynrt_proc_mutex);
		if(drt->num_segments) {
			/* Just rotate between the segments, ignore fixed_rt */
			if(drt->curr_segment >= drt->num_segments)
				drt->curr_segment = 0;
			ret = rds_set_rt(drt->st, drt->rt_segments[drt->curr_segment], 1);
			utils_dbg("[DYNRT] %s, status: %i\n",
				  drt->rt_segments[drt->curr_segment], ret);
			drt->curr_segment++;
		}
		pthread_mutex_unlock(&drt->dynrt_proc_mutex);
		rds_dynpsrt_cond_sleep(&drt->sleep_trig, &drt->sleep_mutex,
				       DYNPS_DELAY_SECS);
	}

	utils_dbg("[DYNRT] Consumer terminated\n");
	return arg;
}

static int
rds_dynrt_get_line(char **res, size_t *len, FILE *file, int line_no)
{
	int ret = 0;

	ret = getline(res, len, file);
	if(ret > RDS_RT_LENGTH) {
		utils_wrn("[DYNRT] Ignoring line longer than 64 chars (ret: %i)\n", ret);
		return -1;
	} else if(ret < 0) {
		if(errno)
			utils_perr("[DYNRT] Failed to read from file, getline()");
		else if(line_no == 0)
			utils_wrn("[DYNRT] Failed to read any lines from file\n");
		else
			utils_dbg("[DYNRT] Got %u lines from file\n", line_no);
		return -1;
	}

	ret = rds_string_sanitize(*res, *len);
	if(ret < 0) {
		utils_wrn("[DYNRT] Malformed string, error: %i\n", ret);
	}

	return ret;
}

static void*
rds_dynrt_filemon_thread(void *arg)
{
	struct rds_dynrt_state *drt = (struct rds_dynrt_state *) arg;
	const struct inotify_event *event = (struct inotify_event*) drt->event_buf;
	char *res = NULL;
	int ret = 0;
	size_t len = 0;
	FILE *file = NULL;
	int i = 0;

	while(drt->active) {
		/* Blocking read until we get an event */
		ret = read(drt->inotify_fd, drt->event_buf, EVENT_LEN);
		if(ret < 0) {
			if (errno == EINTR)
				continue;
			utils_perr("[DYNRT] Failed to read inotify fd, read()");
			continue;
		}

		utils_dbg("[DYNRT] filemon unblocked\n");

		/* Got an ignore event, terminate */
		if(event->mask & IN_IGNORED)
			break;

		file = fopen(drt->filepath, "r");
		if(!file) {
			utils_perr("[DYNRT] Failed to open %s for reading RT segments, fopen()");
			continue;
		}

		pthread_mutex_lock(&drt->dynrt_proc_mutex);
		drt->num_segments = 0;
		for(i = 0; i < DYNRT_MAX_SEGMENTS; i++) {
			ret = rds_dynrt_get_line(&res, &len, file, i);
			if(ret < 0)
				break;

			memset(drt->rt_segments[drt->num_segments], 0, RDS_RT_LENGTH + 1);
			strncpy(drt->rt_segments[drt->num_segments], res, ret);
			drt->num_segments++;
		}
		pthread_mutex_unlock(&drt->dynrt_proc_mutex);
		fclose(file);

		/* If we didn't get anything, fallback to fixed_rt */
		if(drt->num_segments == 0) {
			ret = rds_set_rt(drt->st, drt->fixed_rt, 1);
			utils_dbg("[DYNRT] %s, status: %i (fallback)\n", drt->fixed_rt, ret);
		}
	}

	free(res);
	utils_dbg("[DYNRT] Filemon terminated\n");
	return arg;
}

void
rds_dynrt_destroy(struct rds_dynrt_state *drt)
{
	utils_dbg("[DYNRT] Graceful exit\n");

	drt->active = 0;

	pthread_mutex_lock(&drt->sleep_mutex);
	pthread_cond_signal(&drt->sleep_trig);
	pthread_mutex_unlock(&drt->sleep_mutex);

	if(drt->dynrt_consumer_tid)
		pthread_join(drt->dynrt_consumer_tid, NULL);

	if(drt->inotify_fd && drt->watch_fd)
		inotify_rm_watch(drt->inotify_fd, drt->watch_fd);

	if(drt->inotify_fd)
		close(drt->inotify_fd);

	if(drt->dynrt_filemon_tid)
		pthread_join(drt->dynrt_filemon_tid, NULL);

	rds_set_rt(drt->st, drt->fixed_rt, 1);

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
	drt->watch_fd = inotify_add_watch(drt->inotify_fd, drt->filepath, IN_MODIFY | IN_IGNORED);
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
		ret = -5;
		goto cleanup;
	}

	return ret;

 cleanup:
	rds_dynrt_destroy(drt);
	return ret;
}
