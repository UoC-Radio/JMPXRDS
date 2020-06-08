/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - RTP Server
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

#include "config.h"		/* For DISABLE_RTP_SERVER macro */
#include <jack/jack.h>		/* For jack-related types */
#include <arpa/inet.h>		/* For ipv4 address handling */

#ifdef DISABLE_RTP_SERVER

struct rtp_server {
	jack_client_t *fmmod_client;
};

#else				/* DISABLE_RTP_SERVER */

#include <gst/gst.h>		/* For GStreamer stuff */

struct rtp_server {
	int state;
	jack_client_t *fmmod_client;
	GstBufferPool *pool;
	GstElement *appsrc;
	GstElement *pipeline;
	GstElement *flac_encoder;
	GstElement *rtpbin;
	GstElement *rtpsink;
	GstElement *rtcpsink;
	GstBus *msgbus;
	GMainLoop *loop;
	int init_res;
	uint32_t buf_len;
	int mpx_samplerate;
	int max_samples;
	int baseport;
	struct shm_mapping *ctl_map;
	struct rtp_server_control *ctl;
};

#endif

enum rtp_server_state {
	RTP_SERVER_INACTIVE = 0,
	RTP_SERVER_ACTIVE = 1,
	RTP_SERVER_QUEUE_FULL = 2,
	RTP_SERVER_TERMINATED = 3,
	RTP_SERVER_FAILED = 4
};

#define RTP_SRV_MAX_RECEIVERS	64

struct rtp_server_control {
	pid_t pid;
	struct rtp_server *rtpsrv;
	uint64_t rtp_tx_kbytesps;
	uint64_t rtcp_tx_kbytesps;
	int num_receivers;
	in_addr_t receivers[RTP_SRV_MAX_RECEIVERS];
};

int rtp_server_add_receiver(int addr);
int rtp_server_remove_receiver(int addr);
void rtp_server_send_buffer(const struct rtp_server *rtpsrv, const float *buff,
			    int num_samples);
void rtp_server_destroy(struct rtp_server *rtpsrv);
int rtp_server_init(struct rtp_server *rtpsrv, uint32_t buf_len,
		    int mpx_samplerate, int max_samples, int baseport);
