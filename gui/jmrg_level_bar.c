#include <stdlib.h>	/* For malloc() */
#include <string.h>	/* For memset() */
#include <math.h>	/* For log10() */
#include "jmpxrds_gui.h"

/*********\
* HELPERS *
\*********/

/**
 * IEC standard dB scaling, borrowed from meterbridge (c) Steve Harris
 */
static float
jmrg_level_bar_iec_scale(float db)
{
	float def = 0.0F;	/* Meter deflection %age */

	if (db < -70.0F) {
		def = 0.0F;
	} else if (db < -60.0F) {	/* 0.0 - 2.5 */
		def = (db + 70.0F) * 0.25F;
	} else if (db < -50.0F) {	/* 2.5 - 7.5 */
		def = (db + 60.0F) * 0.5F + 2.5F;
	} else if (db < -40.0F) {	/* 7.5 - 15.0 */
		def = (db + 50.0F) * 0.75F + 7.5F;
	} else if (db < -30.0F) {	/* 15.0 - 30.0 */
		def = (db + 40.0F) * 1.5F + 15.0F;
	} else if (db < -20.0F) {	/* 30.0 - 50.0 */
		def = (db + 30.0F) * 2.0F + 30.0F;
	} else {		/* 50 - 100 */
		def = (db + 20.0F) * 2.5F + 50.0F;
	}

	return def;
}


/*****************\
* POLING FUNCTION *
\*****************/

static int
jmrg_level_bar_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	double val = 0.0L;

	if(!GTK_IS_LEVEL_BAR(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	/* Amplitude to db + iec scaling */
	val = 20.0F * log10((*(float*)vmap->val_ptr));
	val = jmrg_level_bar_iec_scale(val) / 100;

	/* Keep value in range */
	if(val < 0)
		val = 0.0L;
	else if(val > 1.0L)
		val = 1.0L;

	gtk_level_bar_set_value(GTK_LEVEL_BAR(vmap->target),
				(gdouble) val * 30.0L);

	return TRUE;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_level_bar_init(const char* label, float* val_ptr)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *level_bar = NULL;
	GtkStyleContext *context = NULL;
	struct value_map *vmap = NULL;


	/* Use a frame to also have a label there
	 * for free */
	container = gtk_frame_new(label);
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5F, 0.6F);
	if(label != NULL)
		gtk_frame_set_shadow_type(GTK_FRAME(container),
					  GTK_SHADOW_ETCHED_IN);
	else
		gtk_frame_set_shadow_type(GTK_FRAME(container),
					  GTK_SHADOW_NONE);


	/* Use a box to have better control
	 * e.g. align the level bar to the center */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), hbox);


	/* Create the level bar, set it's orientation,
	 * invert it and set its initial value from JMPXRDS */
	level_bar = gtk_level_bar_new();
	if(!level_bar)
		goto cleanup;
	gtk_box_set_center_widget(GTK_BOX(hbox), level_bar);

	/* It's vertical and it's filled from bottom to top */
	gtk_orientable_set_orientation(GTK_ORIENTABLE(level_bar),
					GTK_ORIENTATION_VERTICAL);
	gtk_level_bar_set_inverted(GTK_LEVEL_BAR(level_bar), TRUE);

	/* Make the bar descrete */
	gtk_level_bar_set_mode(GTK_LEVEL_BAR(level_bar),
			       GTK_LEVEL_BAR_MODE_DISCRETE);
	gtk_level_bar_set_min_value(GTK_LEVEL_BAR(level_bar), (gdouble) 0.0L);
	gtk_level_bar_set_max_value(GTK_LEVEL_BAR(level_bar), (gdouble) 30.0L);
	gtk_level_bar_set_value(GTK_LEVEL_BAR(level_bar), (gdouble)(*val_ptr));

	/* Register custom CSS class */
	context = gtk_widget_get_style_context(level_bar);
	gtk_style_context_add_class(context,"gain_bar");

	/* Initialize value_map */
	vmap = (struct value_map*) malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = level_bar;
	vmap->val_ptr = val_ptr;


	/* Register polling function to run at 12FPS
	 * 12FPS -> (1 / 12 * 1000)ms for each frame */
	vmap->esid = g_timeout_add(83, jmrg_level_bar_poll, vmap);

	/* Make sure we clean up the allocated value_map when the level
	 * bar gets destroyed */
	g_signal_connect(level_bar, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	/* Only need the top level container */
	return container;
 cleanup:
	if(level_bar)
		gtk_widget_destroy(level_bar);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
