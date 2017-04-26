#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_rtpstats_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rtp_server_control *ctl = vmap->rtp_ctl;
	char tmp[12] = {0};
	static guint64 prev_rtp_bps = 0;
	static guint64 prev_rtcp_bps = 0;
	int kbps = 0;

	if(!ctl || !GTK_IS_LABEL(vmap->target) || !GTK_IS_LABEL(vmap->target2))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	kbps = (int) ((ctl->rtp_bytes_sent - prev_rtp_bps) / 1024);
	snprintf(tmp, 12, "%i KB/s", kbps);
	gtk_label_set_text(GTK_LABEL(vmap->target), tmp);
	prev_rtp_bps = ctl->rtp_bytes_sent;

	kbps = (int) ((ctl->rtcp_bytes_sent - prev_rtcp_bps) / 1024);
	snprintf(tmp, 12, "%i KB/s", kbps);
	gtk_label_set_text(GTK_LABEL(vmap->target2), tmp);
	prev_rtcp_bps = ctl->rtcp_bytes_sent;

	return TRUE;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_rtpstats_init(struct rtp_server_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *hbox1 = NULL;
	GtkWidget *rtp_kbps_desc = NULL;
	GtkWidget *rtp_kbps = NULL;
	GtkWidget *hbox2 = NULL;
	GtkWidget *rtcp_kbps_desc = NULL;
	GtkWidget *rtcp_kbps = NULL;
	struct value_map *vmap = NULL;

	container = gtk_frame_new("RTP Statistics");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);

	hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox1)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), hbox1, 0, 0, 6);

	rtp_kbps_desc = gtk_label_new("Outgoing RTP:");
	if(!rtp_kbps_desc)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox1), rtp_kbps_desc, 0, 0, 6);

	rtp_kbps = gtk_label_new("0 KB/s");
	if(!rtp_kbps)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(hbox1), rtp_kbps, 0, 0, 6);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox2)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, 0, 0, 6);

	rtcp_kbps_desc = gtk_label_new("Outgoing RTCP:");
	if(!rtcp_kbps_desc)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox2), rtcp_kbps_desc, 0, 0, 6);

	rtcp_kbps = gtk_label_new("0 KB/s");
	if(!rtcp_kbps)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox2), rtcp_kbps, 0, 0, 6);

	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = rtp_kbps;
	vmap->target2 = rtcp_kbps;
	vmap->rtp_ctl = ctl;

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(1000, jmrg_rtpstats_poll, vmap);

	g_signal_connect(container, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(rtcp_kbps)
		gtk_widget_destroy(rtcp_kbps);
	if(rtcp_kbps_desc)
		gtk_widget_destroy(rtcp_kbps_desc);
	if(hbox2)
		gtk_widget_destroy(hbox2);
	if(rtp_kbps)
		gtk_widget_destroy(rtp_kbps);
	if(rtp_kbps_desc)
		gtk_widget_destroy(rtp_kbps_desc);
	if(hbox1)
		gtk_widget_destroy(hbox1);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
