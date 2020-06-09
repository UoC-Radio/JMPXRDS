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

int rtp_server_init(struct rtp_server *rtpsrv, uint32_t buf_len,
		    int mpx_samplerate, int max_samples, int baseport)
{
	return 0;
}

#else				/* DISABLE_RTP_SEVER */

#include "utils.h"
#include <unistd.h>		/* For getpid() */
#include <string.h>		/* For memset() and strstr() */
#include <jack/thread.h>	/* For thread handling through jack */
#include <gst/app/gstappsrc.h>	/* For gst_app_src_* functions */
#include <gst/rtp/gstrtpdefs.h>	/* For GST_RTP_PROFILE_* */

static gboolean
rtp_server_update_stats(gpointer user_data)
{
	const struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	struct rtp_server_control *ctl = rtpsrv->ctl;
	struct in_addr ipv4addr = { 0 };
	static guint64 last_timestamp = 0;
	static guint64 last_rtp_bytes_served = 0;
	static guint64 last_rtcp_bytes_served = 0;
	guint64 cur_timetstamp = 0;
	guint64 cur_rtp_bytes_served = 0;
	guint64 cur_rtcp_bytes_served = 0;
	guint64 timediff = 0;
	guint64 bytediff = 0;
	gchar *clients = NULL;
	char *token = NULL;
	char *str_ptr = NULL;
	char *delim_ptr = NULL;
	int i = 0;
	int ret = 0;

	cur_timetstamp = g_get_monotonic_time();
	timediff = cur_timetstamp - last_timestamp;
	last_timestamp = cur_timetstamp;

	g_object_get(rtpsrv->rtpsink, "bytes-served",
				      &cur_rtp_bytes_served, NULL);
	bytediff = cur_rtp_bytes_served - last_rtp_bytes_served;
	last_rtp_bytes_served = cur_rtp_bytes_served;
	ctl->rtp_tx_kbytesps = (bytediff * 1000000) / (timediff * 1024);

	g_object_get(rtpsrv->rtcpsink, "bytes-served",
				      &cur_rtcp_bytes_served,NULL);
	bytediff = cur_rtcp_bytes_served - last_rtcp_bytes_served;
	last_rtcp_bytes_served = cur_rtcp_bytes_served;
	ctl->rtcp_tx_kbytesps = (bytediff * 1000000) / (timediff * 1024);

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
	g_free(clients);

	ctl->num_receivers = i;

	return TRUE;
}

static void
rtp_server_queue_ready(__attribute__((unused)) GstAppSrc * appsrc,
		       __attribute__((unused)) guint length,
		       gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	rtpsrv->state = RTP_SERVER_ACTIVE;
}

static void
rtp_server_queue_full(__attribute__((unused)) GstAppSrc * appsrc,
		      gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	utils_wrn("[RTP] audiosrc queue overrun\n");
	rtpsrv->state = RTP_SERVER_QUEUE_FULL;
}

static void *
rtp_server_error_cb(__attribute__((unused)) GstBus * bus, GstMessage * msg,
		    gpointer user_data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	gchar *debug_info;
	GError *err;

	/* Print error details */
	gst_message_parse_error(msg, &err, &debug_info);
	utils_err("[RTP] Error received from element %s: %s\n",
		   GST_OBJECT_NAME(msg->src), err->message);
	utils_dbg("[RTP] Debugging information:\n%s\n",
		   debug_info ? debug_info : "none");
	g_clear_error(&err);
	g_free(debug_info);

	rtpsrv->state = RTP_SERVER_FAILED;
	rtp_server_destroy(rtpsrv);
	return rtpsrv;
}

static GstCaps *
rtp_server_request_pt_map_cb(__attribute__((unused)) GstElement *rtpbin,
			     __attribute__((unused)) guint session,
			     guint pt, gpointer user_data)
{
	const struct rtp_server *rtpsrv = (struct rtp_server *)user_data;
	GstCaps *caps = NULL;
	if (pt == 96)
		caps = gst_app_src_get_caps(GST_APP_SRC(rtpsrv->appsrc));
	return caps;
}

static GstElement *
rtp_server_request_aux_sender_cb(__attribute__((unused)) GstElement *rtpbin,
				 guint sessid,
				 __attribute__((unused)) gpointer user_data)
{
	GstElement *rtx;
	GstElement *bin;
	GstPad *pad;
	gchar *name;
	GstStructure *pt_map;

	bin = gst_bin_new (NULL);
	rtx = gst_element_factory_make ("rtprtxsend", NULL);
	pt_map = gst_structure_new ("application/x-rtp-pt-map",
				    "96", G_TYPE_UINT, 97, NULL);
	g_object_set (rtx, "payload-type-map", pt_map, NULL);
	gst_structure_free (pt_map);
	gst_bin_add (GST_BIN (bin), rtx);

	pad = gst_element_get_static_pad (rtx, "src");
	name = g_strdup_printf ("src_%u", sessid);
	gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
	g_free (name);
	gst_object_unref (pad);

	pad = gst_element_get_static_pad (rtx, "sink");
	name = g_strdup_printf ("sink_%u", sessid);
	gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
	g_free (name);
	gst_object_unref (pad);

	return bin;
}

void
rtp_server_send_buffer(const struct rtp_server *rtpsrv, const float *buff,
		       int num_samples)
{
	GstBuffer *gstbuff = NULL;
	GstFlowReturn ret = GST_FLOW_OK;
	GstMapInfo info;

	if (!buff || !num_samples || !rtpsrv ||
	    rtpsrv->state != RTP_SERVER_ACTIVE)
		return;

	/* Get an empty buffer from the pool */
	ret = gst_buffer_pool_acquire_buffer(rtpsrv->pool, &gstbuff, NULL);
	if (ret != GST_FLOW_OK)
		return;

	gst_buffer_map(gstbuff, &info, GST_MAP_WRITE);

	if (G_UNLIKELY(info.size < num_samples * sizeof(float))) {
		gst_buffer_unmap(gstbuff, &info);
		gst_object_unref(gstbuff);
		utils_err("[RTP] GstBufferPool buffer size does not match input\n");
		return;
	}

	/* Copy the data */
	memcpy(info.data, buff, num_samples * sizeof(float));
	gst_buffer_unmap(gstbuff, &info);

	gst_buffer_resize(gstbuff, 0, num_samples * sizeof(float));

	/* Set the buffer's properties */
	GST_BUFFER_TIMESTAMP(gstbuff) = GST_CLOCK_TIME_NONE;
	GST_BUFFER_FLAG_SET(gstbuff, GST_BUFFER_FLAG_LIVE);

	/* Push the buffer to the pipeline through appsrc */
	gst_app_src_push_buffer(GST_APP_SRC(rtpsrv->appsrc), gstbuff);

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
	const struct rtp_server_control *ctl = NULL;
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
	const struct rtp_server_control *ctl = NULL;
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
	GstFlowReturn ret = 0;
	int error = 0;

	/* GSTreamer not initialized */
	if(!gst_is_initialized())
		return;

	switch(rtpsrv->state) {
	case RTP_SERVER_TERMINATED:
		/* Server terminated nothing more to do */
		return;
	case RTP_SERVER_FAILED:
		/* Server failed, no need to send EOS */
		error = 1;
		goto no_eos;
	case RTP_SERVER_INACTIVE:
		/* Server didn't have a chance to run
		 * initialization failed */
		utils_err("[RTP] Initialization failed with code %i\n",
			  rtpsrv->init_res);
		error = 1;
		goto not_running;
	default:
		break;
	}

	utils_dbg("[RTP] Graceful exit\n");

	/* Send EOS, and wait for it to propagate through the
	 * pipeline in case the pipeline is active. */
	ret = gst_app_src_end_of_stream(GST_APP_SRC(rtpsrv->appsrc));
	if(ret == GST_FLOW_OK)
		gst_bus_poll(rtpsrv->msgbus, GST_MESSAGE_EOS, GST_CLOCK_TIME_NONE);

 no_eos:
	/* Stop the main loop */
	g_main_loop_quit(rtpsrv->loop);
	g_main_loop_unref(rtpsrv->loop);

 not_running:
	/* Server terminated, set the state here
	 * to avoid a race condition where rtp_server_destroy
	 * gets called right after the main loop exits and
	 * gst_deinit below hasn't finished. If this happens
	 * gst_deinit will get called again and segfault. */
	rtpsrv->state = RTP_SERVER_TERMINATED;

	/* Release the pipeline */
	if (GST_IS_ELEMENT(rtpsrv->pipeline)) {
		gst_element_set_state(rtpsrv->pipeline, GST_STATE_NULL);
		gst_object_unref(rtpsrv->pipeline);
	}

	/* Release the bus */
	if (GST_IS_BUS(rtpsrv->msgbus)) {
		gst_bus_remove_signal_watch(rtpsrv->msgbus);
		gst_object_unref(rtpsrv->msgbus);
	}

	if (rtpsrv->pool) {
		gst_buffer_pool_set_active(rtpsrv->pool, FALSE);
		gst_object_unref(rtpsrv->pool);
	}

	/* Cleanup the shared memory map */
	utils_shm_destroy(rtpsrv->ctl_map, 1);
	rtpsrv->ctl_map = NULL;
	utils_dbg("[RTP] Control channel closed\n");

	/* Cleanup what's left */
	gst_deinit();

	utils_dbg("[RTP] Destroyed\n");

	/* Signal the parent it's game over, in case we
	 * ended up here due to an error. */
	if (error)
		raise(SIGTERM);
}

/* This comes from FFMpeg for compression level 5
 * (the default on gstreamer's flacenc) */
static int
rtp_get_flac_blocksize(int samplerate)
{
	int target = (samplerate * 105) / 1000;
	int tmp = 0;
	int block_size = 0;
	int i = 0;

	for(i = 0; i < 16; i++) {
		tmp = 256 << i;
		if(tmp <= target)
			block_size = tmp;
	}

	return block_size;
}

static int
rtp_init_audiosrc(struct rtp_server *rtpsrv)
{
	GstCaps *audio_caps = NULL;
	GstAppSrcCallbacks gst_appsrc_cbs;

	rtpsrv->appsrc = gst_element_factory_make("appsrc", "audio_source");
	if (!rtpsrv->appsrc)
		return -1;

	audio_caps = gst_caps_new_simple("audio/x-raw",
					 "rate", G_TYPE_INT,
						rtpsrv->mpx_samplerate,
					 "channels", G_TYPE_INT, 1,
					 "format", G_TYPE_STRING, "F32LE",
					 "layout", G_TYPE_STRING, "interleaved",
					 "channel-mask", GST_TYPE_BITMASK, 0x1,
					 NULL);
	gst_app_src_set_caps(GST_APP_SRC(rtpsrv->appsrc), audio_caps);
	gst_caps_unref(audio_caps);

	gst_app_src_set_stream_type(GST_APP_SRC(rtpsrv->appsrc),
				    GST_APP_STREAM_TYPE_STREAM);
	gst_base_src_set_live(GST_BASE_SRC(rtpsrv->appsrc), TRUE);
	gst_base_src_set_do_timestamp(GST_BASE_SRC(rtpsrv->appsrc), TRUE);
	gst_app_src_set_size(GST_APP_SRC(rtpsrv->appsrc), -1);
	g_object_set(G_OBJECT(rtpsrv->appsrc), "format", GST_FORMAT_TIME, NULL);

	gst_appsrc_cbs.need_data = rtp_server_queue_ready;
	gst_appsrc_cbs.enough_data = rtp_server_queue_full;
	gst_app_src_set_callbacks(GST_APP_SRC(rtpsrv->appsrc),
				  &gst_appsrc_cbs, (gpointer) rtpsrv, NULL);

	return 0;
}

static int
rtp_init_audio_path(struct rtp_server *rtpsrv)
{
	GstElement *audio_converter = NULL;
	GstCaps *audio_caps = NULL;
	int ret = 0;

	/* Initialize appsrc for pushing audio
	 * buffers on the pipeline */
	ret = rtp_init_audiosrc(rtpsrv);
	if (ret < 0)
		return ret;

	/* Initialize audio converter since FLAC encoder accepts only integer
	 * formats */
	audio_converter = gst_element_factory_make("audioconvert",
						   "audio_converter");
	if (!audio_converter)
		return -2;

	/* Initialize FLAC encoder */
	rtpsrv->flac_encoder = gst_element_factory_make("flacenc", "flac_encoder");
	if (!rtpsrv->flac_encoder)
		return -3;
	g_object_set(rtpsrv->flac_encoder, "blocksize",
		     rtp_get_flac_blocksize(rtpsrv->mpx_samplerate), NULL);

	/* Add audio path elements to the pipeline and link them */

	gst_bin_add_many(GST_BIN(rtpsrv->pipeline), rtpsrv->appsrc,
			 audio_converter, rtpsrv->flac_encoder, NULL);

	ret = gst_element_link(rtpsrv->appsrc, audio_converter);
	if (!ret)
		return -4;

	/* Convert from 24bits to 16bits to save some more bandwidth,
	 * after all we compress the audio for FM broadcasting so the
	 * dynamic range is reduced anyway. */
	audio_caps = gst_caps_new_simple("audio/x-raw",
					 "format", G_TYPE_STRING, "S16LE",
					 NULL);
	ret = gst_element_link_filtered(audio_converter, rtpsrv->flac_encoder,
					audio_caps);
	gst_caps_unref(audio_caps);
	if (!ret)
		return -5;

	return 0;
}

static int
rtp_init_network_path(struct rtp_server *rtpsrv)
{
	GstElement *rtp_payloader = NULL;
	GstElement *rtcpsrc = NULL;
	GstPad *srcpad = NULL;
	GstPad *sinkpad = NULL;
	int ret = 0;

	/* Initialize RTP payloader, since there is no spec for FLAC
	 * use the GStreamer buffer payloader. We'll use GStreamer
	 * on the other side too so it's not an issue. */
	rtp_payloader = gst_element_factory_make("rtpgstpay", "rtp_payloader");
	if (!rtp_payloader)
		return -1;
	g_object_set(rtp_payloader, "config-interval", 3, NULL);
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtp_payloader);

	/* Link flac encoder to RTP payloader */
	ret = gst_element_link(rtpsrv->flac_encoder, rtp_payloader);
	if (!ret)
		return -2;

	/* Initialize the rtpbin element and add it to the pipeline */
	rtpsrv->rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
	if (!rtpsrv->rtpbin)
		return -3;
	/* Audio/Video profile with feedback (AVPF) */
	g_object_set(rtpsrv->rtpbin, "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);
	g_signal_connect (rtpsrv->rtpbin, "request-pt-map",
			  G_CALLBACK (rtp_server_request_pt_map_cb), rtpsrv);
	/* register callback to create "rtprtxsend".
	 * This needs to be called before requesting the pads from rtpbin */
	g_signal_connect (rtpsrv->rtpbin, "request-aux-sender",
			  G_CALLBACK (rtp_server_request_aux_sender_cb), NULL);
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtpsrv->rtpbin);

	/* Set up an RTP sinkpad for session 0 from rtpbin and link it to the
	 * rtp_payloader */
	srcpad = gst_element_get_static_pad(rtp_payloader, "src");
	sinkpad = gst_element_get_request_pad(rtpsrv->rtpbin, "send_rtp_sink_0");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK)
		return -4;
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Initialize the UDP sink for outgoing RTP messages */
	rtpsrv->rtpsink = gst_element_factory_make("multiudpsink", "rtpsink");
	if (!rtpsrv->rtpsink)
		return -5;
	g_object_set(rtpsrv->rtpsink, "bind-port", rtpsrv->baseport, NULL);
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtpsrv->rtpsink);

	/* Get the RTP srcpad that was created for session 0 above and
	 * link it to rtpsink */
	srcpad = gst_element_get_static_pad(rtpsrv->rtpbin, "send_rtp_src_0");
	sinkpad = gst_element_get_static_pad(rtpsrv->rtpsink, "sink");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK)
		return -6;
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Initialize the UDP src for incoming RTCP messages */
	rtcpsrc = gst_element_factory_make("udpsrc", "rtcpsrc");
	if (!rtcpsrc)
		return -7;
	g_object_set(rtcpsrc, "port", rtpsrv->baseport + 2, NULL);
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtcpsrc);

	/* In order to receive RTCP messages link rtcpsrc to
	 * rtpbin's recv_rtcp_sink for session 0 */
	srcpad = gst_element_get_static_pad(rtcpsrc, "src");
	sinkpad = gst_element_get_request_pad(rtpsrv->rtpbin, "recv_rtcp_sink_0");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK)
		return -8;
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	/* Initialize the UDP sink for outgoing RTCP messages */
	rtpsrv->rtcpsink = gst_element_factory_make("multiudpsink", "rtcpsink");
	if (!rtpsrv->rtcpsink)
		return -9;
	/* no need for synchronisation or preroll on the RTCP sink */
	g_object_set(rtpsrv->rtcpsink, "async", FALSE, "sync", FALSE, NULL);
	g_object_set(rtpsrv->rtpsink, "bind-port", rtpsrv->baseport + 1, NULL);
	gst_bin_add(GST_BIN(rtpsrv->pipeline), rtpsrv->rtcpsink);

	/* Get the RTCP srcpad that was created for session 0 above and
	 * link it to rtcpbin */
	srcpad = gst_element_get_request_pad(rtpsrv->rtpbin, "send_rtcp_src_0");
	sinkpad = gst_element_get_static_pad(rtpsrv->rtcpsink, "sink");
	if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK)
		return -10;
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);

	return 0;
}

static void *
_rtp_server_init(void *data)
{
	struct rtp_server *rtpsrv = (struct rtp_server *)data;
	GstStructure *config = NULL;
	int ret = 0;

	/* Set state to inactive */
	rtpsrv->state = RTP_SERVER_INACTIVE;

	/* Initialize I/O channel */
	rtpsrv->ctl_map = utils_shm_init(RTP_SRV_SHM_NAME,
					 sizeof(struct rtp_server_control));
	if(!rtpsrv->ctl_map) {
		ret = -1;
		goto cleanup;
	}
	rtpsrv->ctl = (struct rtp_server_control*) rtpsrv->ctl_map->mem;
	utils_dbg("[RTP] Control channel ready\n");

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

	/* Initialize audio and network paths */

	ret = rtp_init_audio_path(rtpsrv);
	if (ret < 0) {
		utils_err("[RTP] Initializing audio path failed with code %i\n", ret);
		ret = -3;
		goto cleanup;
	}

	ret = rtp_init_network_path(rtpsrv);
	if (ret < 0) {
		utils_err("[RTP] Initializing network path failed with code %i\n", ret);
		ret = -4;
		goto cleanup;
	}

	/* Configure a buffer pool with a minimum of 3
	 * buffers pre-allocated */
	rtpsrv->pool = gst_buffer_pool_new();
	config = gst_buffer_pool_get_config(rtpsrv->pool);
	gst_buffer_pool_config_set_params(config, NULL,
					  rtpsrv->buf_len, 3, 0);
	if (!gst_buffer_pool_set_config(rtpsrv->pool, config) ||
	    !gst_buffer_pool_set_active(rtpsrv->pool, TRUE)) {
		ret = -5;
		goto cleanup;
	}

	/* Update the stats every 1 sec */
	g_timeout_add_seconds(1, rtp_server_update_stats, (gpointer) rtpsrv);

	/* We are ready, set the pipeline to playing state and
	 * create a main loop for the server to receive messages */
	if(gst_element_set_state(rtpsrv->pipeline, GST_STATE_PLAYING) ==
	   GST_STATE_CHANGE_FAILURE) {
		ret = -6;
		goto cleanup;
	}

	utils_dbg("[RTP] Init complete\n");

	rtpsrv->state = RTP_SERVER_ACTIVE;
	rtpsrv->loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(rtpsrv->loop);

 cleanup:
	rtpsrv->init_res = ret;
	rtp_server_destroy(rtpsrv);
	return (void *)rtpsrv;
}

int
rtp_server_init(struct rtp_server *rtpsrv, uint32_t buf_len,
		int mpx_samplerate, int max_samples, int baseport)
{
	int ret = 0;
	static jack_native_thread_t tid = 0;

	rtpsrv->buf_len = buf_len;
	rtpsrv->mpx_samplerate = mpx_samplerate;
	rtpsrv->max_samples = max_samples;
	rtpsrv->baseport = baseport;

	if (tid == 0) {
		/* If thread doesn't exist create it */
		ret = jack_client_create_thread(rtpsrv->fmmod_client, &tid,
						jack_client_real_time_priority(rtpsrv->fmmod_client),
						jack_is_realtime(rtpsrv->fmmod_client),
						_rtp_server_init,
						(void *)rtpsrv);
		if (ret < 0) {
			utils_err("[JACKD] Could not create processing thread\n");
			return -1;
		} else if (rtpsrv->init_res != 0)
			return rtpsrv->init_res;
	}

	return 0;
}

#endif	/* DISABLE_RTP_SERVER */
