#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include <time.h>	/* For nanosleep() */
#include "jmpxrds_gui.h"

struct rbutton_group {
	struct fmmod_control *ctl;
	GtkWidget *rbuttons[4];
	int type;
	guint	esid;
};

#define RBG_TYPE_MODULATION	0
#define	RBG_TYPE_FMPREEMPH	1

/*********\
* HELPERS *
\*********/

/* Manages the group of radio buttons for chosing modulation */
static gboolean
jmrg_fmmodp_radio_buttons_update(gpointer data)
{
	struct rbutton_group *rbgrp = (struct rbutton_group*) data;
	float scgain = rbgrp->ctl->stereo_carrier_gain;
	int mode = 0;
	int i = 0;
	static int old_mod_mode = 0;
	static int old_pe_mode = 0;
	static int alpf_state = 0;
	static int doubled = 0;
	gboolean active = FALSE;

	if(!gtk_widget_is_visible(rbgrp->rbuttons[0]))
		return TRUE;

	if(rbgrp->type == RBG_TYPE_FMPREEMPH)
		goto preemph;

	mode = rbgrp->ctl->stereo_modulation;
	if(mode > 4)
		mode = FMMOD_DSB;

	if(mode == old_mod_mode)
		return TRUE;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rbgrp->rbuttons[mode]));
	if(active == FALSE)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rbgrp->rbuttons[mode]),
					     TRUE);

	/* When switching to LP filter-based SSB, double
	 * the stereo carrier gain */
	if((mode == FMMOD_SSB_LPF) && !doubled) {
		if(scgain > 1.0)
			scgain = 2.0;
		else
			scgain *= 2.0;
		rbgrp->ctl->stereo_carrier_gain = scgain;
		doubled = 1;
	} else if((mode != FMMOD_SSB_LPF) && doubled) {
		scgain *= 0.5;
		rbgrp->ctl->stereo_carrier_gain = scgain;
		doubled = 0;
	}

	old_mod_mode = mode;

	return TRUE;

 preemph:
	mode = rbgrp->ctl->preemph_tau;
	if(mode > 2)
		mode = LPF_PREEMPH_NONE;

	if(alpf_state != rbgrp->ctl->use_audio_lpf) {
		if(!rbgrp->ctl->use_audio_lpf)
			for(i = 0; i < 3; i++)
				gtk_widget_set_sensitive(rbgrp->rbuttons[i], 0);
		else
			for(i = 0; i < 3; i++)
				gtk_widget_set_sensitive(rbgrp->rbuttons[i], 1);
	}
	alpf_state = rbgrp->ctl->use_audio_lpf;

	if(mode == old_pe_mode)
		return TRUE;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rbgrp->rbuttons[mode]));
	if(active == FALSE)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rbgrp->rbuttons[mode]),
					     TRUE);

	old_pe_mode = mode;

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_fmmodp_free_rbutton_group(__attribute__((unused)) GtkWidget *widget,
			       gpointer data)
{
	const struct timespec tv = {0, 20000000L};
	struct rbutton_group *rbgrp = (struct rbutton_group*) data;
	g_source_remove(rbgrp->esid);
	nanosleep(&tv, NULL);
	free(rbgrp);
	return;
}


/****************\
* FMMOD CONTROLS *
\****************/

static GtkWidget*
jmrg_fmmodp_fmdc_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	struct rbutton_group *rbgrp = NULL;
	int i = 0;
	int ret = 0;

	rbgrp = (struct rbutton_group*) malloc(sizeof(struct rbutton_group));
	if(!rbgrp) {
		ret = -1;
		goto cleanup;
	}
	memset(rbgrp, 0, sizeof(struct rbutton_group));
	rbgrp->ctl = ctl;

	/* Greate a frame and put a vertical box inside */
	container = gtk_frame_new("FM Stereo Modulation Mode");
	if(!container) {
		ret = -2;
		goto cleanup;
	}
	gtk_frame_set_label_align(GTK_FRAME(container), 0.1, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);


	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	if(!vbox) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_set_homogeneous(GTK_BOX(vbox), TRUE);
	gtk_container_add(GTK_CONTAINER(container), vbox);


	/* Initialize radio button group */
	rbgrp->rbuttons[0] = jmrg_radio_button_init("DSB (Default)",
					    &ctl->stereo_modulation,
					    FMMOD_DSB, NULL);
	if(!rbgrp->rbuttons[0]) {
		ret = -4;
		goto cleanup;
	}

	rbgrp->rbuttons[1] = jmrg_radio_button_init("SSB (Hartley)",
					    &ctl->stereo_modulation,
					    FMMOD_SSB_HARTLEY,
					    GTK_RADIO_BUTTON(rbgrp->rbuttons[0]));
	if(!rbgrp->rbuttons[1]) {
		ret = -5;
		goto cleanup;
	}


	rbgrp->rbuttons[2] = jmrg_radio_button_init("SSB (LP Filter)",
					    &ctl->stereo_modulation,
					    FMMOD_SSB_LPF,
					    GTK_RADIO_BUTTON(rbgrp->rbuttons[1]));
	if(!rbgrp->rbuttons[2]) {
		ret = -6;
		goto cleanup;
	}

	rbgrp->rbuttons[3] = jmrg_radio_button_init("Mono",
					    &ctl->stereo_modulation,
					    FMMOD_MONO,
					    GTK_RADIO_BUTTON(rbgrp->rbuttons[2]));
	if(!rbgrp->rbuttons[3]) {
		ret = -7;
		goto cleanup;
	}

	/* Now put them on the box */
	for(i = 0; i < 4; i++)
		gtk_box_pack_start(GTK_BOX(vbox), rbgrp->rbuttons[i],
				   TRUE, TRUE, 2);

	/* Register polling function and signal handlers */
	rbgrp->esid = g_timeout_add(200, jmrg_fmmodp_radio_buttons_update, rbgrp);
	rbgrp->type = RBG_TYPE_MODULATION;

	g_signal_connect(container, "unrealize",
			 G_CALLBACK(jmrg_fmmodp_free_rbutton_group),
			 rbgrp);

	return container;
 cleanup:
	if(rbgrp) {
		for(i = 0; i < 4; i++)
			if(rbgrp->rbuttons[i])
				gtk_widget_destroy(rbgrp->rbuttons[i]);
		free(rbgrp);
	}
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	utils_err("[FMMOD CTLS] Init failed with code: %i\n", ret);
	return NULL;
}


/***********************\
* AUDIO FILTER CONTROLS *
\***********************/

static GtkWidget*
jmrg_fmmodp_audio_filter_ctls_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *pe_frame = NULL;
	GtkWidget *pe_vbox = NULL;
	GtkWidget *lpf_sw = NULL;
	struct rbutton_group *rbgrp = NULL;
	int i = 0;
	int ret = 0;

	rbgrp = (struct rbutton_group*) malloc(sizeof(struct rbutton_group));
	if(!rbgrp) {
		ret = -1;
		goto cleanup;
	}
	memset(rbgrp, 0, sizeof(struct rbutton_group));
	rbgrp->ctl = ctl;

	container = gtk_frame_new("Audio Filter Controls");
	if(!container) {
		ret = -2;
		goto cleanup;
	}
	gtk_frame_set_label_align(GTK_FRAME(container), 0.1, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox) {
		ret = -3;
		goto cleanup;
	}
	gtk_container_add(GTK_CONTAINER(container), vbox);

	lpf_sw = jmrg_switch_init("Low pass filter", &ctl->use_audio_lpf);
	if(!lpf_sw) {
		ret = -4;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(vbox), lpf_sw, 0, 0, 6);

	pe_frame = gtk_frame_new("FM Pre-emphasis");
	if(!container) {
		ret = -5;
		goto cleanup;
	}
	gtk_frame_set_label_align(GTK_FRAME(pe_frame), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(pe_frame),
				  GTK_SHADOW_ETCHED_IN);
	gtk_box_pack_start(GTK_BOX(vbox), pe_frame, 0, 0, 6);

	pe_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!pe_vbox) {
		ret = -6;
		goto cleanup;
	}
	gtk_box_set_homogeneous(GTK_BOX(pe_vbox), TRUE);
	gtk_container_add(GTK_CONTAINER(pe_frame), pe_vbox);

	/* Initialize radio button group */
	rbgrp->rbuttons[0] = jmrg_radio_button_init("50μsec (World)",
					    (int*) &ctl->preemph_tau,
					    LPF_PREEMPH_50US, NULL);
	if(!rbgrp->rbuttons[0]) {
		ret = -7;
		goto cleanup;
	}

	rbgrp->rbuttons[1] = jmrg_radio_button_init("75μsec (U.S.A.)",
					    (int*) &ctl->preemph_tau,
					    LPF_PREEMPH_75US,
					    GTK_RADIO_BUTTON(rbgrp->rbuttons[0]));
	if(!rbgrp->rbuttons[1]) {
		ret = -8;
		goto cleanup;
	}

	rbgrp->rbuttons[2] = jmrg_radio_button_init("No pre-emphasis",
					    (int*) &ctl->preemph_tau,
					    LPF_PREEMPH_NONE,
					    GTK_RADIO_BUTTON(rbgrp->rbuttons[1]));
	if(!rbgrp->rbuttons[2]) {
		ret = -9;
		goto cleanup;
	}

	/* Now put them on the box */
	for(i = 0; i < 3; i++)
		gtk_box_pack_start(GTK_BOX(pe_vbox), rbgrp->rbuttons[i],
				   TRUE, TRUE, 2);

	/* Register polling function and signal handlers */
	rbgrp->esid = g_timeout_add(200, jmrg_fmmodp_radio_buttons_update, rbgrp);
	rbgrp->type = RBG_TYPE_FMPREEMPH;

	g_signal_connect(container, "unrealize",
			 G_CALLBACK(jmrg_fmmodp_free_rbutton_group),
			 rbgrp);

	return container;
 cleanup:
	if(container)
		gtk_widget_destroy(container);
	utils_err("[AUDIO FILTER CTLS] Init failed with code: %i\n", ret);
	return NULL;
}


/*******************\
* MPX GAIN CONTROLS *
\*******************/

static GtkWidget *
jmrg_fmmodp_audio_gain_ctls_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *audl_lvl = NULL;
	GtkWidget *audr_lvl = NULL;
	GtkWidget *augain_ctl = NULL;

	container = gtk_frame_new("Audio");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);
	gtk_widget_set_margin_bottom(container, 6);

	/* Create sub-container */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_container_add(GTK_CONTAINER(container), hbox);

	/* Create Level bars for audio and audio gain control */
	audl_lvl = jmrg_level_bar_init("L", &ctl->peak_audio_in_l);
	if(!audl_lvl)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), audl_lvl, 1, 1, 6);
	gtk_widget_set_margin_bottom(audl_lvl, 6);

	augain_ctl = jmrg_vscale_init(NULL, &ctl->audio_gain, (gdouble) 100.0);
	if(!augain_ctl)
		goto cleanup;
	gtk_box_set_center_widget(GTK_BOX(hbox), augain_ctl);

	audr_lvl = jmrg_level_bar_init("R", &ctl->peak_audio_in_r);
	if(!audr_lvl)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(hbox), audr_lvl, 1, 1, 6);
	gtk_widget_set_margin_bottom(audr_lvl, 6);

	return container;
 cleanup:
	if(augain_ctl)
		gtk_widget_destroy(augain_ctl);
	if(audl_lvl)
		gtk_widget_destroy(audl_lvl);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}

static GtkWidget*
jmrg_fmmodp_subcarrier_gain_ctls_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *sgain_ctl = NULL;
	GtkWidget *rgain_ctl = NULL;

	container = gtk_frame_new("Subcarriers");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);
	gtk_widget_set_margin_bottom(container, 6);

	/* Create sub-container */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_container_add(GTK_CONTAINER(container), hbox);

	/* Create sub-carrier gain controls */
	sgain_ctl = jmrg_vscale_init("Stereo", &ctl->stereo_carrier_gain,
				     (gdouble) 200.0);
	if(!sgain_ctl)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), sgain_ctl, 1, 1, 6);

	rgain_ctl = jmrg_vscale_init("RDS", &ctl->rds_gain,
				     (gdouble) 10.0);
	if(!rgain_ctl)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(hbox), rgain_ctl, 1, 1, 6);

	return container;
 cleanup:
	if(sgain_ctl)
		gtk_widget_destroy(sgain_ctl);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}

static GtkWidget*
jmrg_fmmodp_mpx_gain_ctl_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *mpxgain_ctl = NULL;
	GtkWidget *mpx_lvl = NULL;

	container = gtk_frame_new("MPX");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);
	gtk_widget_set_margin_bottom(container, 6);

	/* Create sub-container */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_container_add(GTK_CONTAINER(container), hbox);

	/* A scale and a level bar for the whole MPX signal */
	mpxgain_ctl = jmrg_vscale_init(NULL, &ctl->mpx_gain,
				       (gdouble) 200.0);
	if(!mpxgain_ctl)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), mpxgain_ctl, 1, 1, 1);

	mpx_lvl = jmrg_level_bar_init(NULL, &ctl->peak_mpx_out);
	if(!mpx_lvl)
		goto cleanup;
	gtk_widget_set_margin_bottom(mpx_lvl, 6);
	gtk_box_pack_end(GTK_BOX(hbox), mpx_lvl, 1, 1, 6);

	return container;
 cleanup:
	if(mpxgain_ctl)
		gtk_widget_destroy(mpxgain_ctl);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}

static GtkWidget*
jmrg_fmmodp_mpxgc_init(struct fmmod_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *aud_ctls = NULL;
	GtkWidget *pgain_ctl = NULL;
	GtkWidget *scgains_ctls = NULL;
	GtkWidget *mpxgain_ctl = NULL;
	int ret = 0;

	/* Create top level container for MPX Gain controls */
	container = gtk_frame_new("MPX Gain Controls");
	if(!container) {
		ret = -1;
		goto cleanup;
	}
	gtk_frame_set_label_align(GTK_FRAME(container), 0.1, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);


	/* Create sub-container */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	if(!hbox) {
		ret = -2;
		goto cleanup;
	}
	gtk_container_add(GTK_CONTAINER(container), hbox);

	/* Audio gain controls */
	aud_ctls = jmrg_fmmodp_audio_gain_ctls_init(ctl);
	if(!aud_ctls) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), aud_ctls, 1, 1, 6);

	/* Pilot gain control */
	pgain_ctl = jmrg_vscale_init("Pilot", &ctl->pilot_gain,
				     (gboolean) 20.0);
	if(!pgain_ctl) {
		ret = -4;
		goto cleanup;
	}
	gtk_widget_set_margin_bottom(pgain_ctl, 6);
	gtk_box_pack_start(GTK_BOX(hbox), pgain_ctl, 1, 1, 6);

	/* Subcarrier gain control */
	scgains_ctls = jmrg_fmmodp_subcarrier_gain_ctls_init(ctl);
	if(!scgains_ctls) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), scgains_ctls, 1, 1, 6);

	/* MPX gain control */
	mpxgain_ctl = jmrg_fmmodp_mpx_gain_ctl_init(ctl);
	if(!mpxgain_ctl) {
		ret = -6;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), mpxgain_ctl, 1, 1, 6);

	return container;
 cleanup:
	if(scgains_ctls)
		gtk_widget_destroy(scgains_ctls);
	if(pgain_ctl)
		gtk_widget_destroy(pgain_ctl);
	if(aud_ctls)
		gtk_widget_destroy(aud_ctls);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	utils_err("[MPX GAIN CTLS] Init failed with code: %i\n", ret);
	return NULL;
}


/*************\
* ENTRY POINT *
\*************/

int
jmrg_fmmod_panel_init(struct control_page *ctl_page)
{
	GtkWidget *hbox = NULL;
	GtkWidget *lvbox = NULL;
	GtkWidget *rvbox = NULL;
	GtkWidget *uocr_logo = NULL;
	GdkPixbuf *pixbuf_uocr_logo = NULL;
	struct fmmod_control *ctl = NULL;
	GtkWidget *mpxp = NULL;
	GtkWidget *mpxgc = NULL;
	GtkWidget *fmdc = NULL;
	GtkWidget *afc = NULL;
	int ret = 0;

	memset(ctl_page, 0, sizeof(struct control_page));

	/* Attach shared memory to talk with JMPXRDS */
	ctl_page->shmem = utils_shm_attach(FMMOD_CTL_SHM_NAME,
					   sizeof(struct fmmod_control));
	if(!ctl_page->shmem) {
		utils_perr("Unable to communicate with JMPXRDS");
		ret = -1;
		goto cleanup;
	}
	ctl = (struct fmmod_control*) ctl_page->shmem->mem;

	/* Create sub-container */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	if(!hbox) {
		ret = -2;
		goto cleanup;
	}

	/* Initialize left vertical box */
	lvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	if(!lvbox) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), lvbox, TRUE, TRUE, 2);

	/* Initialize mpx plotter */
	mpxp = jmrg_mpx_plotter_init(ctl->sample_rate, ctl->max_samples);
	if(!mpxp) {
		ret = -4;
		goto cleanup;
	}
	gtk_widget_set_size_request(mpxp, 560, 366);
	gtk_box_pack_start(GTK_BOX(lvbox), mpxp, TRUE, TRUE, 2);

	/* Initialize mpx gain controls */
	mpxgc = jmrg_fmmodp_mpxgc_init(ctl);
	if(!mpxgc) {
		ret = -5;
		goto cleanup;
	}
	gtk_widget_set_size_request(mpxgc, 560, 300);
	gtk_box_pack_start(GTK_BOX(lvbox), mpxgc, FALSE, FALSE, 2);


	/* Initialize right vertical box */
	rvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	if(!rvbox) {
		ret = -6;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), rvbox, FALSE, FALSE, 2);

	/* Initialize FMmod controls */
	fmdc = jmrg_fmmodp_fmdc_init(ctl);
	if(!fmdc) {
		ret = -7;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(rvbox), fmdc, FALSE, FALSE, 2);

	/* Initialize Audio filter controls */
	afc = jmrg_fmmodp_audio_filter_ctls_init(ctl);
	if(!afc) {
		ret = -8;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(rvbox), afc, FALSE, FALSE, 2);


	/* Initialize UoC Radio logo image */
	pixbuf_uocr_logo = gdk_pixbuf_new_from_file_at_scale(
						DATA_PATH"uoc_radio_logo.png",
						180, 180, TRUE, NULL);
	if (!pixbuf_uocr_logo) {
		pixbuf_uocr_logo = gdk_pixbuf_new_from_file_at_scale(
						"gui/images/uoc_radio_logo.png",
						180, 180, TRUE, NULL);
		if (!pixbuf_uocr_logo) {
			ret = -9;
			goto cleanup;
		}
	}

	uocr_logo = gtk_image_new_from_pixbuf(pixbuf_uocr_logo);
	if (!uocr_logo) {
		ret = -10;
		goto cleanup;
	}
	gtk_box_pack_end(GTK_BOX(rvbox), uocr_logo, FALSE, FALSE, 30);

	/* Set the label and container for the page */
	ctl_page->label = gtk_label_new("MPX Generator");
	if(!ctl_page->label) {
		ret = -11;
		goto cleanup;
	}
	ctl_page->container = hbox;

	/* Register signal hanlder for unrealize */
	g_signal_connect(hbox, "unrealize", G_CALLBACK(jmrg_panel_destroy),
			 ctl_page);

	return 0;
 cleanup:
	if(uocr_logo)
		gtk_widget_destroy(uocr_logo);
	if(pixbuf_uocr_logo)
		g_object_unref(pixbuf_uocr_logo);
	if(afc)
		gtk_widget_destroy(afc);
	if(fmdc)
		gtk_widget_destroy(fmdc);
	if(rvbox)
		gtk_widget_destroy(rvbox);
	if(mpxgc)
		gtk_widget_destroy(mpxgc);
	if(mpxp)
		gtk_widget_destroy(mpxp);
	if(lvbox)
		gtk_widget_destroy(lvbox);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(ctl_page->shmem)
		utils_shm_destroy(ctl_page->shmem, 0);
	utils_err("[FMMOD PANEL] Init failed with code: %i\n", ret);
	return ret;
}
