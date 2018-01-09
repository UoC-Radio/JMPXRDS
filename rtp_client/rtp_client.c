/*
 * JMPXRDS, an FM MPX signal generator with RDS support on
 * top of Jack Audio Connection Kit - Standalone RTP Client
 *
 * Copyright (C) 2017 George Kiagiadakis <gkiagia@tolabaki.gr>
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

#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include <glib-unix.h>

#define DEFAULT_LATENCY 200
#define DEFAULT_RTP_PORT 5000
#define DEFAULT_REMOTE_RTCP_PORT 5002
#define DEFAULT_DEVICE "default"

struct rtp_client
{
	GMainLoop *loop;
	GstElement *pipeline;
	GstElement *sink_bin;
	GstCaps *recv_caps;
};

static void
print_statistics (struct rtp_client *client)
{
	g_autoptr (GObject) rtpbin = NULL;
	g_autoptr (GObject) session = NULL;
	g_autoptr (GstStructure) stats = NULL;
	g_autofree gchar *str = NULL;

	rtpbin = gst_child_proxy_get_child_by_name (
		GST_CHILD_PROXY (client->pipeline), "rtpbin");
	g_signal_emit_by_name (rtpbin, "get-session", 0, &session);
	g_object_get (session, "stats", &stats, NULL);

	/* simply dump the stats structure */
	str = gst_structure_to_string (stats);
	g_print ("Statistics: %s\n", str);
}

static gboolean
sigusr1_cb (gpointer user_data)
{
	struct rtp_client *client = user_data;
	print_statistics (client);
	return G_SOURCE_CONTINUE;
}

static gboolean
signal_cb (gpointer user_data)
{
	struct rtp_client *client = user_data;

	g_print ("JMPXRDS RTP Client exiting...\n");

	g_main_loop_quit (client->loop);
	return G_SOURCE_REMOVE;
}

static void
error_cb(GstBus *bus, GstMessage *msg, gpointer user_data)
{
	g_autoptr (GError) err = NULL;
	g_autofree gchar *debug_info = NULL;
	struct rtp_client *client = user_data;

	/* Print error details */
	gst_message_parse_error (msg, &err, &debug_info);
	g_printerr ("Error received from element %s: %s\n",
			GST_OBJECT_NAME(msg->src), err->message);
	g_printerr ("Debugging information: %s\n",
			debug_info ? debug_info : "none");

	g_main_loop_quit (client->loop);
}

static GstElement *
request_aux_receiver (GstElement *rtpbin, guint sessid, gpointer user_data)
{
	GstElement *rtx, *bin;
	GstPad *pad;
	gchar *name;
	GstStructure *pt_map;

	bin = gst_bin_new (NULL);
	rtx = gst_element_factory_make ("rtprtxreceive", NULL);
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

static GstCaps *
request_pt_map (GstElement *rtpbin, guint session, guint pt, gpointer user_data)
{
	struct rtp_client *client = user_data;
	return (pt == 96) ? gst_caps_ref (client->recv_caps) : NULL;
}

static void
rtpbin_pad_added (GstElement *rtpbin, GstPad *src, gpointer user_data)
{
	struct rtp_client *client = user_data;
	g_autoptr (GstPad) sink = NULL;

	if (g_str_has_prefix (GST_PAD_NAME (src), "recv_rtp_src_")) {
		sink = gst_element_get_static_pad (client->sink_bin, "sink");
		if (G_UNLIKELY (gst_pad_is_linked (sink))) {
			g_autoptr (GstPad) old_src = gst_pad_get_peer (sink);
			gst_pad_unlink (old_src, sink);
		}

		gst_pad_link (src, sink);
		gst_element_sync_state_with_parent (client->sink_bin);
	}
}

static void
rtpbin_pad_removed (GstElement *rtpbin, GstPad *src, gpointer user_data)
{
	struct rtp_client *client = user_data;
	g_autoptr (GstPad) sink = NULL;

	if (g_str_has_prefix (GST_PAD_NAME (src), "recv_rtp_src_")) {
		sink = gst_element_get_static_pad (client->sink_bin, "sink");
		gst_pad_unlink (src, sink);
		gst_element_set_state (client->sink_bin, GST_STATE_PAUSED);
	}
}

static gboolean
initialize(struct rtp_client *client, int *argc, char **argv[])
{
	g_autoptr (GOptionContext) context = NULL;
	g_autoptr (GError) error = NULL;
	g_autoptr (GstBus) bus = NULL;
	g_autoptr (GstElement) rtpbin = NULL;
	g_autoptr (GstElement) rtpsrc = NULL;

	gint latency = DEFAULT_LATENCY;
	gint rtp_port = DEFAULT_RTP_PORT;
	g_autofree gchar *remote_host = NULL;
	gint remote_rtcp_port = DEFAULT_REMOTE_RTCP_PORT;
	g_autofree gchar *device = NULL;

	const GOptionEntry entries[] =
	{
		{ "latency", 'l', 0, G_OPTION_ARG_INT, &latency,
		  "Amount of ms to buffer in the jitterbuffers",
		  G_STRINGIFY (DEFAULT_LATENCY) },
		{ "port", 'p', 0, G_OPTION_ARG_INT, &rtp_port,
		  "Port to listen for RTP packets (and RTCP in port+1)",
		  G_STRINGIFY (DEFAULT_RTP_PORT) },
		{ "remote-host", 'h', 0, G_OPTION_ARG_STRING, &remote_host,
		  "Address of host to send RTCP packets to", "" },
		{ "remote-rtcp-port", 'r', 0, G_OPTION_ARG_INT,
		   &remote_rtcp_port, "Port to send RTCP packets to",
		  G_STRINGIFY (DEFAULT_REMOTE_RTCP_PORT) },
		{ "device", 'd', 0, G_OPTION_ARG_STRING, &device,
		  "ALSA device to output the FM signal to", DEFAULT_DEVICE },
		{ NULL }
	};

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, "receive FM MPX from JMPXRDS");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_add_group (context, gst_init_get_option_group ());

	if (!g_option_context_parse (context, argc, argv, &error)) {
		g_printerr ("option parsing failed: %s\n", error->message);
		return FALSE;
	}

	if (!(client->pipeline = gst_parse_launch ("rtpbin name=rtpbin "
		"udpsrc name=rtpsrc "
		"udpsrc name=rtcpsrc ! rtpbin.recv_rtcp_sink_0 "
		"rtpbin.send_rtcp_src_0 ! udpsink name=rtcpsink", &error)))
	{
		g_printerr ("constructing the pipeline failed: %s\n",
				error->message);
		return FALSE;
	}

	if (!(client->sink_bin = gst_parse_bin_from_description (
		"rtpgstdepay name=depayloader ! flacparse"
		"  ! flacdec name=flac_decoder ! audioconvert "
		"  ! alsasink name=audio_sink", TRUE, &error)))
	{
		g_printerr ("constructing the sink bin failed: %s\n",
				error->message);
		g_object_unref (client->pipeline);
		return FALSE;
	}

	/* consume the floating reference so that we always hold one ref */
	g_object_ref_sink (client->sink_bin);
	gst_bin_add (GST_BIN (client->pipeline), client->sink_bin);

	client->recv_caps = gst_caps_new_simple ("application/x-rtp",
		"media", G_TYPE_STRING, "application",
		"clock-rate", G_TYPE_INT, 90000,
		"encoding-name", G_TYPE_STRING, "X-GST", NULL);

	gst_child_proxy_set (GST_CHILD_PROXY (client->pipeline),
		"rtpbin::latency", latency,
		"rtpbin::do-retransmission", TRUE,
		"rtpbin::rtp-profile", GST_RTP_PROFILE_AVPF,
		"rtpsrc::caps", client->recv_caps,
		"rtpsrc::port", rtp_port,
		"rtcpsrc::port", rtp_port + 1,
		"rtcpsink::host", remote_host,
		"rtcpsink::port", remote_rtcp_port,
		"rtcpsink::sync", FALSE,
		"rtcpsink::async", FALSE,
		NULL);
	gst_child_proxy_set (GST_CHILD_PROXY (client->sink_bin),
		"flac_decoder::plc", TRUE,
		"audio_sink::device", device ? device : DEFAULT_DEVICE,
		NULL);

	bus = gst_pipeline_get_bus (GST_PIPELINE (client->pipeline));
	g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), client);
	gst_bus_add_signal_watch (bus);

	rtpbin = gst_bin_get_by_name (GST_BIN (client->pipeline), "rtpbin");
	g_signal_connect (rtpbin, "request-aux-receiver",
		G_CALLBACK (request_aux_receiver), client);
	g_signal_connect (rtpbin, "request-pt-map",
		G_CALLBACK (request_pt_map), client);
	g_signal_connect (rtpbin, "pad-added",
		G_CALLBACK (rtpbin_pad_added), client);
	g_signal_connect (rtpbin, "pad-removed",
		G_CALLBACK (rtpbin_pad_removed), client);

	/* This link needs to happen after we have connected the
	 * "request-aux-receiver" signal, because rtpbin internally
	 * calls our callback to create rtprtxreceive while it is
	 * creating the "recv_rtp_sink_0" pad
	 */
	rtpsrc = gst_bin_get_by_name (GST_BIN (client->pipeline), "rtpsrc");
	gst_element_link_pads (rtpsrc, "src", rtpbin, "recv_rtp_sink_0");

	return TRUE;
}

int
main(int argc, char *argv[])
{
	struct rtp_client client = {0};

	if (!initialize (&client, &argc, &argv))
		return 1;

	client.loop = g_main_loop_new (NULL, FALSE);

	/* install signal handler to exit gracefully */
	g_unix_signal_add (SIGHUP, signal_cb, &client);
	g_unix_signal_add (SIGINT, signal_cb, &client);
	g_unix_signal_add (SIGTERM, signal_cb, &client);
	g_unix_signal_add (SIGUSR1, sigusr1_cb, &client);

	/* run the pipeline */
	gst_element_set_state (client.pipeline, GST_STATE_PLAYING);
	g_main_loop_run (client.loop);
	gst_element_set_state (client.pipeline, GST_STATE_NULL);

	/* cleanup */
	gst_caps_unref (client.recv_caps);
	gst_object_unref (client.pipeline);
	g_main_loop_unref (client.loop);

	return 0;
}

