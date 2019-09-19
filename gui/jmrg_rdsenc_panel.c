#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"


/**************\
* STATION INFO *
\**************/

static GtkWidget*
jmrg_rdsc_station_info_init(struct rds_encoder_state *st)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *top_hbox = NULL;
	GtkWidget *ps = NULL;
	GtkWidget *pi = NULL;
	GtkWidget *bottom_hbox = NULL;
	GtkWidget *ecc = NULL;
	GtkWidget *lic = NULL;

	container = gtk_frame_new("Station Info");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);

	top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!top_hbox)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), top_hbox, 1, 1, 6);

	ps = jmrg_display_field_init(st, "Programme Service Name (PSN)",
				     RDS_FIELD_PS);
	if(!ps)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(top_hbox), ps, 1, 1, 6);

	pi = jmrg_display_field_init(st, "Programme Identifier (PI)",
				     RDS_FIELD_PI);
	if(!pi)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(top_hbox), pi, 0, 0, 6);

	bottom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!bottom_hbox)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), bottom_hbox, 1, 1, 6);

	ecc = jmrg_acentry_init(st, "Country", RDS_FIELD_ECC);
	if(!ecc)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(bottom_hbox), ecc, 1, 1, 6);

	lic = jmrg_acentry_init(st, "Language", RDS_FIELD_LIC);
	if(!lic)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(bottom_hbox), lic, 1, 1, 6);

	return container;
 cleanup:
	if(ecc)
		gtk_widget_destroy(ecc);
	if(bottom_hbox)
		gtk_widget_destroy(bottom_hbox);
	if(pi)
		gtk_widget_destroy(pi);
	if(ps)
		gtk_widget_destroy(ps);
	if(top_hbox)
		gtk_widget_destroy(top_hbox);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}


/****************\
* PROGRAMME INFO *
\****************/

static GtkWidget *
jmrg_rdsc_programme_flags_init(struct rds_encoder_state *st)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *ms_switch = NULL;
	GtkWidget *ta = NULL;
	GtkWidget *tp = NULL;

	container = gtk_frame_new("Programme Flags");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.1, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);

	ms_switch = jmrg_checkbox_init(st, "Music / Speech switch (MS)",
				       RDS_FIELD_MS, 1, 0);
	if(!ms_switch)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(vbox), ms_switch, 0, 0, 6);

	ta = jmrg_checkbox_init(st, "Traffic Announcement", RDS_FIELD_TA, 1, 0);
	if(!ta)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), ta, 0, 0, 6);

	tp = jmrg_checkbox_init(st, "Traffic Programme", RDS_FIELD_TP, 1, 0);
	if(!tp)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(vbox), tp, 0, 0, 6);

	return container;
 cleanup:
	if(ta)
		gtk_widget_destroy(ta);
	if(ms_switch)
		gtk_widget_destroy(ms_switch);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}

static GtkWidget*
jmrg_rdsc_programme_info_init(struct rds_encoder_state *st)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *ptyn = NULL;
	GtkWidget *pty = NULL;
	GtkWidget *programme_flags = NULL;

	container = gtk_frame_new("Programme Info");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), hbox);

	ptyn = jmrg_display_field_init(st, "Programme Type Name (PTYN)",
				       RDS_FIELD_PTYN);
	if(!ptyn)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), ptyn, 1, 1, 6);

	pty = jmrg_cbox_text_init(st, "Programme Type (PTY)", RDS_FIELD_PTY);
	if(!pty)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), pty, 0, 0, 6);

	programme_flags = jmrg_rdsc_programme_flags_init(st);
	if(!programme_flags)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(hbox), programme_flags, 0, 0, 6);

	return container;
 cleanup:
	if(pty)
		gtk_widget_destroy(pty);
	if(ptyn)
		gtk_widget_destroy(ptyn);
	if(hbox)
		gtk_widget_destroy(container);
	return NULL;
}


/**************\
* DECODER INFO *
\**************/

static GtkWidget*
jmrg_rdsc_decoder_info_init(struct rds_encoder_state *st)
{
	GtkWidget *container = NULL;
	GtkWidget *fbox = NULL;
	GtkWidget *stereo = NULL;
	GtkWidget *art_head = NULL;
	GtkWidget *compressed = NULL;
	GtkWidget *dynpty = NULL;

	container = gtk_frame_new("Decoder Info");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.1, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	fbox = gtk_flow_box_new();
	if(!fbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), fbox);

	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fbox), 2);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fbox), 2);

	stereo = jmrg_checkbox_init(st, "Stereo", RDS_FIELD_DI,
				    RDS_DI_STEREO, 0);
	if(!stereo)
		goto cleanup;
	gtk_flow_box_insert(GTK_FLOW_BOX(fbox), stereo, -1);

	art_head = jmrg_checkbox_init(st, "Artificial Head", RDS_FIELD_DI,
				      RDS_DI_ARTIFICIAL_HEAD, 0);
	if(!art_head)
		goto cleanup;
	gtk_flow_box_insert(GTK_FLOW_BOX(fbox), art_head, -1);

	compressed = jmrg_checkbox_init(st, "Compressed", RDS_FIELD_DI,
					RDS_DI_COMPRESSED, 0);
	if(!compressed)
		goto cleanup;
	gtk_flow_box_insert(GTK_FLOW_BOX(fbox), compressed, -1);

	dynpty = jmrg_checkbox_init(st, "Dynamic PTY", RDS_FIELD_DI,
				    RDS_DI_DYNPTY, 0);
	if(!dynpty)
		goto cleanup;
	gtk_flow_box_insert(GTK_FLOW_BOX(fbox), dynpty, -1);

	return container;
 cleanup:
	if(compressed)
		gtk_widget_destroy(compressed);
	if(art_head)
		gtk_widget_destroy(art_head);
	if(stereo)
		gtk_widget_destroy(stereo);
	if(fbox)
		gtk_widget_destroy(fbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}


/*************\
* ENTRY POINT *
\*************/

int
jmrg_rdsenc_panel_init(struct control_page *ctl_page)
{
	GtkWidget *container = NULL;
	GtkWidget *station_info = NULL;
	GtkWidget *radiotext = NULL;
	GtkWidget *programme_info = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *lvbox = NULL;
	GtkWidget *mvbox = NULL;
	GtkWidget *rvbox = NULL;
	GtkWidget *decoder_info = NULL;
	GtkWidget *encoder_onoff_sw = NULL;
	GtkWidget *rds_logo = NULL;
	GdkPixbuf *pixbuf_rds_logo = NULL;
	struct rds_encoder_state *st = NULL;
	int ret = 0;

	/* Attach shared memory to talk with JMPXRDS */
	ctl_page->shmem = utils_shm_attach(RDS_ENC_SHM_NAME,
					   sizeof(struct rds_encoder_state));
	if(!ctl_page->shmem) {
		utils_perr("Unable to communicate with JMPXRDS");
		ret = -1;
		goto cleanup;
	}
	st = (struct rds_encoder_state*) ctl_page->shmem->mem;

	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!container) {
		ret = -1;
		goto cleanup;
	}
	ctl_page->container = container;

	station_info = jmrg_rdsc_station_info_init(st);
	if(!station_info) {
		ret = -2;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), station_info, 0, 0, 6);

	radiotext = jmrg_display_field_init(st, "RadioText",
					    RDS_FIELD_RT);
	if(!radiotext) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), radiotext, 0, 0, 6);

	programme_info = jmrg_rdsc_programme_info_init(st);
	if(!programme_info) {
		ret = -4;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), programme_info, 0, 0, 6);


	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox) {
		ret = -4;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), hbox, 0, 0, 6);

	lvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!lvbox) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), lvbox, 1, 1, 6);
	
	decoder_info = jmrg_rdsc_decoder_info_init(st);
	if(!decoder_info) {
		ret = -4;
		goto cleanup;
	}
	gtk_box_set_center_widget(GTK_BOX(lvbox), decoder_info);

	mvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!mvbox) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), mvbox, 0, 0, 6);

	encoder_onoff_sw = jmrg_switch_init("Enable / Disable RDS Encoder",
				 	    (int*) &st->enabled);
	if(!encoder_onoff_sw) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_set_center_widget(GTK_BOX(mvbox), encoder_onoff_sw);

	rvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!rvbox) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_pack_end(GTK_BOX(hbox), rvbox, 0, 0, 6);

	/* Initialize RDS logo image */
	pixbuf_rds_logo = gdk_pixbuf_new_from_file_at_scale(
						DATA_PATH"rds_logo.png",
						162, 44, TRUE, NULL);
	if(!pixbuf_rds_logo) {
		pixbuf_rds_logo = gdk_pixbuf_new_from_file_at_scale(
						"gui/images/rds_logo.png",
						162, 44, TRUE, NULL);
		if(!pixbuf_rds_logo) {
				ret = -13;
			goto cleanup;
		}
	}

	rds_logo = gtk_image_new_from_pixbuf(pixbuf_rds_logo);
	if(!rds_logo) {
		ret = -14;
		goto cleanup;
	}
	gtk_box_pack_end(GTK_BOX(rvbox), rds_logo, 1, 1, 20);


	/* Set the label for the page */
	ctl_page->label = gtk_label_new("RDS Encoder");
	if(!ctl_page->label) {
		ret = -15;
		goto cleanup;
	}

	/* Register signal hanlder for unrealize */
	g_signal_connect(hbox, "unrealize", G_CALLBACK(jmrg_panel_destroy),
			 ctl_page);

	return 0;
 cleanup:
	if(rds_logo)
		gtk_widget_destroy(rds_logo);
	if(pixbuf_rds_logo)
		g_object_unref(pixbuf_rds_logo);
	if(rvbox)
		gtk_widget_destroy(rvbox);
	if(encoder_onoff_sw)
		gtk_widget_destroy(encoder_onoff_sw);
	if(mvbox)
		gtk_widget_destroy(mvbox);
	if(decoder_info)
		gtk_widget_destroy(decoder_info);
	if(lvbox)
		gtk_widget_destroy(lvbox);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(programme_info)
		gtk_widget_destroy(programme_info);
	if(radiotext)
		gtk_widget_destroy(radiotext);
	if(station_info)
		gtk_widget_destroy(station_info);
	if(container)
		gtk_widget_destroy(container);
	if(ctl_page->shmem)
		utils_shm_destroy(ctl_page->shmem, 0);
	utils_err("[RDSENC PANEL] Init failed with code: %i\n", ret);
	return ret;
}
