#include <stdlib.h>	/* For malloc() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/******************\
* POLLING FUNCTION *
\******************/

static int
jmrg_vscale_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;

	if(!GTK_IS_SCALE(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	/* Gain values go from 0 to 1, scale value goes from 0 to 100 */
	gtk_range_set_value(GTK_RANGE(vmap->target),
			    (gdouble) (*(float*)vmap->val_ptr) * 100.0L);

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static gchar*
jmrg_vscale_format_value(GtkScale *scale, gdouble value)
{
	return g_strdup_printf("%0*g%%", gtk_scale_get_digits (scale), value);
}

static void
jmrg_vscale_update(GtkRange *range, gpointer data)
{
	gdouble new_val = 0.0L;
	float *val_ptr = (float*) data;
	new_val = gtk_range_get_value(range);
	/* Gain values go from 0 to 1, scale value goes from 0 to 100 */
	(*val_ptr) = (float) new_val * 0.01;
	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_vscale_init(const char* label, float* val_ptr, gdouble max)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *vscale = NULL;
	struct value_map *vmap = NULL;

	/* Use a frame to also have a label there
	 * for free */
	container = gtk_frame_new(label);
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	if(label != NULL)
		gtk_frame_set_shadow_type(GTK_FRAME(container),
					  GTK_SHADOW_ETCHED_IN);
	else
		gtk_frame_set_shadow_type(GTK_FRAME(container),
					  GTK_SHADOW_NONE);


	/* Use a box to have better control */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), hbox);


	/* Provide a max value here since each scale may have a different
	 * maximum */
	vscale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL,
					  0.0, max, 6.0);
	if(!vscale)
		goto cleanup;
	gtk_box_set_center_widget(GTK_BOX(hbox), vscale);
	gtk_range_set_inverted(GTK_RANGE(vscale), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(vscale), GTK_POS_TOP);
	gtk_range_set_value(GTK_RANGE(vscale), (*val_ptr) * 100);

	/* Also put some marks to make sliding easier */
	gtk_scale_add_mark(GTK_SCALE(vscale), max, GTK_POS_LEFT, NULL);
	gtk_scale_add_mark(GTK_SCALE(vscale), max / (gdouble) 2.0,
			   GTK_POS_LEFT, NULL);
	gtk_scale_add_mark(GTK_SCALE(vscale), 0, GTK_POS_LEFT, NULL);


	/* Initialize value_map */
	vmap = (struct value_map*) malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = vscale;
	vmap->val_ptr = val_ptr;

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_vscale_poll, vmap);

	g_signal_connect(vscale, "format_value",
			 G_CALLBACK(jmrg_vscale_format_value), NULL);

	g_signal_connect(vscale, "value-changed",
			 G_CALLBACK(jmrg_vscale_update),
			 (gpointer) val_ptr);

	g_signal_connect(vscale, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	/* Only need the top level container */
	return container;	
 cleanup:
	if(vscale)
		gtk_widget_destroy(vscale);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
