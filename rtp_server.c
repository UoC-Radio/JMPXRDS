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

#include "rtp_server.h"

#ifdef DISABLE_RTP_SERVER

int rtp_server_add_receiver(int addr)
{
	return 0;
}

int rtp_server_remove_receiver(int addr)
{
	return 0;
}

void
rtp_server_send_buffer(struct rtp_server *rtpsrv, float *buff, int num_samples)
{
	return;
}

void rtp_server_destroy(struct rtp_server *rtpsrv)
{
	return;
}

int
rtp_server_init(struct rtp_server *rtpsrv, int mpx_samplerate,
		int max_samples, int baseport)
{
	return 0;
}

#else				/* DISABLE_RTP_SEVER */

#include "utils.h"
#include <unistd.h>		/* For getpid() */
#include <string.h>		/* For memset() and strstr() */
#include <jack/thread.h>	/* For thread handling through jack */
#include <gst/app/gstappsrc.h>	/* For gst_app_src_* functions */

static volatile sig_atomic_t rtpsrv_state = RTP_SERVER_INACTIVE;

static gboolean
rtp_server_update_stats(gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	struct rtp_server_control *ctl = rtpsrv->ctl;
	struct in_addr ipv4addr = { 0 };
	gchar *clients = NULL;
	char *token = NULL;
	char *str_ptr = NULL;
	char *delim_ptr = NULL;
	int i = 0;
	int ret = 0;

	g_object_get(rtpsrv->rtpsink, "bytes-served",
				      &ctl->rtp_bytes_sent, NULL);
	g_object_get(rtpsrv->rtcpsink, "bytes-served",
				      &ctl->rtcp_bytes_sent,NULL);

	/* Do this only for rtpsink, they are supposed to have the same receivers
	 * anyway */
	g_object_get(rtpsrv->rtpsink, "clients", &clients, NULL);
	str_ptr = clients;
	while ((token = strtok_r(str_ptr, ",", &str_ptr))
	       && (i < RTP_SRV_MAX_RECEIVERS)) {
		/* Find : and replace it with \0 */
		delim_ptr = strchr(token, ':');
		(*delim_ptr) = '\0';
		/* Parse IP address and put its integer representation
		 * on the array of receivers */
		ret = inet_aton(token, &ipv4addr);
		if (ret) {
			ctl->receivers[i] = ipv4addr.s_addr;
			i++;
		}
	}

	ctl->num_receivers = i;

	return TRUE;
}

static void
rtp_server_queue_ready(GstAppSrc * appsrc, guint length, gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	rtpsrv_state = RTP_SERVER_ACTIVE;
}

static void
rtp_server_queue_full(GstAppSrc * appsrc, gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	rtpsrv_state = RTP_SERVER_QUEUE_FULL;
}

static void *
rtp_server_error_cb(GstBus * bus, GstMessage * msg, gpointer user_data)
{
	GError *err;
	gchar *debug_info;
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;

	/* Print error details */
	gst_message_parse_error(msg, &err, &debug_info);
	g_printerr("Error received from element %s: %s\n",
		   GST_OBJECT_NAME(msg->src), err->message);
	g_printerr("Debugging information: %s\n",
		   debug_info ? debug_info : "none");
	g_clear_error(&err);
	g_free(debug_info);
	g_main_loop_quit(rtpsrv->loop);
}

void
rtp_server_send_buffer(struct rtp_server *rtpsrv, float *buff, int num_samples)
{
	GstBuffer *gstbuff = NULL;
	GstFlowReturn ret = 0;

	if (!buff || !num_samples || !rtpsrv ||
	    rtpsrv_state != RTP_SERVER_ACTIVE)
		return;

	guint gstbuff_len = 0;

	/* Wrap the output buffer to a GStreamer buffer */
	gstbuff_len = num_samples * sizeof(float);
	gstbuff = gst_buffer_new_wrapped_full(0, (gpointer) buff, gstbuff_len,
					      0, gstbuff_len, NULL, NULL);

	/* Set the buffer's properties */
	GST_BUFFER_TIMESTAMP(gstbuff) = GST_CLOCK_TIME_NONE;
	GST_BUFFER_FLAG_SET(gstbuff, GST_BUFFER_FLAG_LIVE);

	/* Push the buffer to the pipeline through appsrc */
	ret = gst_app_src_push_buffer(GST_APP_SRC(rtpsrv->appsrc), gstbuff);

	/* Ignore any errors for now */
	return;
}

int
rtp_server_add_receiver(int addr)
{
	int ret = 0;
	char *ipv4string;
	struct in_addr ipv4addr = { 0 };
	struct shm_mapping *shmem = NULL;
	struct rtp_server_control *ctl = NULL;
	struct rtp_server *rtpsrv = NULL;
	gchar *clients = NULL;
	int rtpsinkok = 0;
	int rtcpsinkok = 0;

	ipv4addr.s_addr = addr;

	shmem = utils_shm_attach(RTP_SRV_SHM_NAME,
				 sizeof(struct rtp_server_control));
	if (!shmem)
		return -1;
	ctl = (struct rtp_server_control*) shmem->mem;
	rtpsrv = ctl->rtpsrv;

	ipv4string = inet_ntoa(ipv4addr);

	/* Add to rtpsink and verify */
	g_object_get(rtpsrv->rtpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) == NULL)
		g_signal_emit_by_name(rtpsrv->rtpsink, "add", ipv4string,
				      rtpsrv->baseport, NULL);
	g_free(clients);

	g_object_get(rtpsrv->rtpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) != NULL)
		rtpsinkok = 1;
	g_free(clients);

	/* Same for rtcpsink */
	g_object_get(rtpsrv->rtcpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) == NULL)
		g_signal_emit_by_name(rtpsrv->rtcpsink, "add", ipv4string,
				      rtpsrv->baseport + 1, NULL);
	g_free(clients);

	g_object_get(rtpsrv->rtcpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) != NULL)
		rtcpsinkok = 1;
	g_free(clients);

	if (!rtpsinkok || !rtcpsinkok) {
		/* Just in case it was added on only one of them */
		rtp_server_remove_receiver(addr);
		ret = -1;
	} else
		rtp_server_update_stats((gpointer) rtpsrv);

	/* Clear the shm mapping */
	utils_shm_destroy(shmem, 0);

	return ret;
}

int
rtp_server_remove_receiver(int addr)
{
	int ret = 0;
	char *ipv4string;
	struct in_addr ipv4addr = { 0 };
	struct shm_mapping *shmem = NULL;
	struct rtp_server_control *ctl = NULL;
	struct rtp_server *rtpsrv = NULL;
	gchar *clients = NULL;
	int rtpsinkok = 0;
	int rtcpsinkok = 0;

	ipv4addr.s_addr = addr;

	shmem = utils_shm_attach(RTP_SRV_SHM_NAME,
				 sizeof(struct rtp_server_control));
	if (!shmem)
		return -1;
	ctl = (struct rtp_server_control*) shmem->mem;
	rtpsrv = ctl->rtpsrv;

	ipv4string = inet_ntoa(ipv4addr);

	/* Remove from rtpsink and verify */
	g_signal_emit_by_name(rtpsrv->rtpsink, "remove", ipv4string,
			      rtpsrv->baseport, NULL);
	g_object_get(rtpsrv->rtpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) == NULL)
		rtpsinkok = 1;
	g_free(clients);

	/* Same for rtcpsink */
	g_signal_emit_by_name(rtpsrv->rtcpsink, "remove", ipv4string,
			      rtpsrv->baseport + 1, NULL);
	g_object_get(rtpsrv->rtcpsink, "clients", &clients, NULL);
	if (strstr(clients, ipv4string) == NULL)
		rtcpsinkok = 1;
	g_free(clients);

	/*XXX: No idea what to do on this case, is it even possible ? */
	if (!rtpsinkok || !rtcpsinkok)
		ret = -1;
	else
		rtp_server_update_stats((gpointer) rtpsrv);

	/* Clear the shm mapping */
	utils_shm_destroy(shmem, 0);

	return ret;
}

void
rtp_server_destroy(struct rtp_server *rtpsrv)
{
	rtpsrv_state = RTP_SERVER_INACTIVE;

	utils_shm_destroy(rtpsrv->ctl_map, 1);

	/* Send EOS and wait for it to propagate through the
	 * pipeline */
	if (rtpsrv->appsrc) {
		gst_app_src_end_of_stream(GST_APP_SRC(rtpsrv->appsrc));
		gst_bus_poll(rtpsrv->msgbus, GST_MESSAGE_EOS,
			     GST_CLOCK_TIME_NONE);
	}

	if (rtpsrv->pipeline) {
		gst_element_set_state(rtpsrv->pipeline, GST_STATE_NULL);
		gst_object_unref(rtpsrv->pipeline);
	}

	if (rtpsrv->msgbus) {
		gst_bus_remove_signal_watch(rtpsrv->msgbus);
		gst_object_unref(rtpsrv->msgbus);
	}

	if (rtpsrv->loop) {
		g_main_loop_quit(rtpsrv->loop);
		g_main_loop_unref(rtpsrv->loop);
	}

	gst_deinit();
}

static void *
_rtp_server_init(void *data)
{
	int ret = 0;
	GstCaps *audio_caps = NULL;
	GstElement *audio_converter = NULL;
	GstElement *flac_encoder = NULL;
	GstElement *rtp_payloader = NULL;
	GstElement *rtcpsrc = NULL;
	GstElement *rtprtxqueue = NULL;
	GstPad *srcpad = NULL;
	GstPad *sinkpad = NULL;
	GstAppSrcCallbacks gst_appsrc_cbs;
	struct rtp_server *rtpsrv = (struct rtp_server *)data;

	/* Initialize I/O channel */
	rtpsrv->ctl_map = utils_shm_init(RTP_SRV_SHM_NAME,
					 sizeof(struct rtp_server_control));
	if(!rtpsrv->ctl_map) {
		ret = -1;
		goto cleanup;
	}
	rtpsrv->ctl = (struct rtp_server_control*) rtpsrv->ctl_map->mem;
	/* Store the pointer to rtpsrv so that we can recover it
	 * when called by the signal handler */
	rtpsrv->ctl->rtpsrv = rtpsrv;
	/* Store the pid so that the control app knows where to
	 * send the signals to add / remove client IPs */
	rtpsrv->ctl->pid = getpid();

	/* Initialize GStreamer */
	gst_init(NULL, NULL);

	/* Initialize Pipeline and its GSTbus */
	rtpsrv->pipeline = gst_pipeline_new("pipeline");
	if (!rtpsrv->pipeline) {
		ret = -2;
		goto cleanup;
	}
	rtpsrv->msgbus = gst_element_get_bus(rtpsrv->pipeline);
	gst_bus_add_signal_watch(rtpsrv->msgbus);
	g_signal_connect(G_OBJECT(rtpsrv->msgbus), "message::error",
			 (GCallback) rtp_server_error_cb, rtpsrv);

	/* Initialize appsrc for pushing audio
	 * buffers on the pipeline */
	rtpsrv->appsrc = gst_element_factory_make("appsrc", "audio_source");
	if (!rtpsrv->appsrc) {
		ret = -3;
		goto cleanup;
	}

	audio_caps = gst_caps_new_simple("audio/x-raw",
					 "rate", G_TYPE_INT,
						rtpsrv->mpx_samplerate,
					 "channels", G_TYPE_INT, 1,
					 "format", G_TYPE_STRING, "F32LE",
					 "layout", G_TYPE_STRING, "interleaved",
					 "channel-mask", GST_TYPE_BITMASK, 0x1,
					 NULL);

	gst_app_src_set_caps(GST_APP_SRC(rtpsrv->appsrc), audio_caps);

	gst_app_src_set_stream_type(GST_APP_SRC(rtpsrv->appsrc),
				    GST_APP_STREAM_TYPE_STREAM);
	gst_base_src_set_live(GST_BASE_SRC(rtpsrv->appsrc), TRUE);
	gst_app_src_set_size(GST_APP_SRC(rtpsrv->appsrc), -1);
	g_object_set(G_OBJECT(rtpsrv->appsrc), "format", GST_FORMAT_TIME, NULL);

	gst_appsrc_cbs.need_data = rtp_server_queue_ready;
	gst_appsrc_cbs.enough_data = rtp_server_queue_full;
	gst_app_src_set_callbacks(GST_APP_SRC(rtpsrv->appsrc),
				  &gst_appsrc_cbs, (gpointer) rtpsrv, NULL);

	/* Initialize audio converter since FLAC encoder accepts only integer
	 * formats */
	audio_converter = gst_element_factory_make("audioconvert",
						   "audio_converter");
	if (!audio_converter) {
		ret = -4;
		goto cleanup;
	}

	/* Initialize FLAC encoder */
	flac_encoder = gst_element_factory_make("flacenc", "flac_encoder");
	if (!flac_encoder) {
		ret = -5;
		goto cleanup;
	}

	/* Initialize RTP payloader, since there is no spec for FLAC
	 * use the GStreamer buffer payloader. We'll use GStreamer
	 * on the other side too so it's not an issue. */
	rtp_payloader = gst_element_factory_make("rtpgstpay", "rtp_payloader");
	if (!rtp_payloader) {
		ret = -6;
		goto cleanup;
	}
	g_object_set(rtp_payloader, "config-interval", 3, NULL);

	/* Initialize RTP rtx queue to support non-RFC compliant RTP
	 * retransmissions */
	rtprtxqueue = gst_element_factory_make("rtprtxqueue", "rtprtxqueue");
	if (!rtprtxqueue) {
		ret = -7;
		goto cleanup;
	}

	/* Create the pipeline down to the rtprtxqueue */
	gst_bin_add_many(GST_BIN(rtpsrv->pipeline), rtpsrv->appsrc,
			 audio_converter, flac_encoder, rtp_payloader,
			 rtprtxqueue, NULL);

	ret = gst_element_link_many(rtpsrv->appsrc, audio_converter,
				    flac_encoder, rtp_payloader, rtprtxqueue,
				    NULL);
	if (!ret) {
		ret = -8;
		goto cleanup;
	}

	/* Initialize the rtpbin element and add it to the pipeline */
	rtpsrv->rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
	if (!rtpsrv->rtpbin) {
		ret = -9;
		goto cleanup;
	}
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtpsrv->rtpbin);

	/* Initialize the UDP sink for outgoing RTP messages and add localhost
	 * to the list of receivers */
	rtpsrv->rtpsink = gst_element_factory_make("multiudpsink", "rtpsink");
	if (!rtpsrv->rtpsink) {
		ret = -10;
		goto cleanup;
	}
	g_signal_emit_by_name(rtpsrv->rtpsink, "add", "127.0.0.1",
			      rtpsrv->baseport, NULL);

	/* Initialize the UDP sink for outgoing RTCP messages and add localhost
	 * to the list of receivers */
	rtpsrv->rtcpsink = gst_element_factory_make("multiudpsink", "rtcpsink");
	if (!rtpsrv->rtcpsink) {
		ret = -11;
		goto cleanup;
	}
	g_signal_emit_by_name(rtpsrv->rtcpsink, "add", "127.0.0.1",
			      rtpsrv->baseport + 1, NULL);

	/* no need for synchronisation or preroll on the RTCP sink */
	g_object_set(rtpsrv->rtcpsink, "async", FALSE, "sync", FALSE, NULL);

	/* Initialize the UDP src for incoming RTCP messages */
	rtcpsrc = gst_element_factory_make("udpsrc", "rtcpsrc");
	if (!rtcpsrc) {
		ret = -12;
		goto cleanup;
	}
	g_object_set(rtcpsrc, "port", rtpsrv->baseport + 2, NULL);

	/* Add the UDP sources and sinks to the pipeline */
	gst_bin_add_many(GST_BIN(rtpsrv->pipeline), rtpsrv->rtpsink,
			 rtpsrv->rtcpsink, rtcpsrc, NULL);

	/* Set up an RTP sinkpad for session 0 from rtpbin and link it to the
	 * rtprtxqueue */
	srcpad = gst_element_get_static_pad(rtprtxqueue, "src");
	sinkpad = gst_element_get_request_pad(rtpsrv->rtpbin, "send_rtp_sink_0");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
		ret = -13;
		goto cleanup;
	}
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Get the RTP srcpad that was created for session 0 above and
	 * link it to rtpsink */
	srcpad = gst_element_get_static_pad(rtpsrv->rtpbin, "send_rtp_src_0");
	sinkpad = gst_element_get_static_pad(rtpsrv->rtpsink, "sink");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
		ret = -14;
		goto cleanup;
	}
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Get the RTCP srcpad that was created for session 0 above and
	 * link it to rtcpbin */
	srcpad = gst_element_get_request_pad(rtpsrv->rtpbin, "send_rtcp_src_0");
	sinkpad = gst_element_get_static_pad(rtpsrv->rtcpsink, "sink");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
		ret = -15;
		goto cleanup;
	}
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* In order to receive RTCP messages link rtcpsrc to
	 * rtpbin's recv_rtcp_sink for session 0 */
	srcpad = gst_element_get_static_pad(rtcpsrc, "src");
	sinkpad = gst_element_get_request_pad(rtpsrv->rtpbin, "recv_rtcp_sink_0");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
		ret = -16;
		goto cleanup;
	}
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Update the stats every 1 sec */
	g_timeout_add_seconds(1, rtp_server_update_stats, (gpointer) rtpsrv);

	/* We are ready, set the pipeline to playing state and
	 * create a main loop for the server to receive messages */
	gst_element_set_state(rtpsrv->pipeline, GST_STATE_PLAYING);
	rtpsrv_state = RTP_SERVER_ACTIVE;
	rtpsrv->loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(rtpsrv->loop);

 cleanup:
	rtp_server_destroy(rtpsrv);
	rtpsrv->init_res = ret;
	return (void *)rtpsrv;
}

int
rtp_server_init(struct rtp_server *rtpsrv, int mpx_samplerate, int max_samples,
		int baseport)
{
	int ret = 0;
	int rtprio = 0;
	static jack_native_thread_t tid = 0;

	rtpsrv->mpx_samplerate = mpx_samplerate;
	rtpsrv->max_samples = max_samples;
	rtpsrv->baseport = baseport;

	rtprio = jack_client_max_real_time_priority(rtpsrv->fmmod_client);
	if (rtprio < 0)
		return -1;

	if (tid == 0) {
		/* If thread doesn't exist create it */
		ret = jack_client_create_thread(rtpsrv->fmmod_client, &tid,
						rtprio, 1,
						_rtp_server_init,
						(void *)rtpsrv);
		if (ret < 0 || rtpsrv->init_res != 0)
			return -1;
	}

	return 0;
}

#endif				/* DISABLE_RTP_SERVER */
