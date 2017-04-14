#include <stdlib.h>	/* For malloc() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_switch_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	int active = (int) gtk_switch_get_active(GTK_SWITCH(vmap->target));

	if(!GTK_IS_SWITCH(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	if(gtk_widget_has_focus(vmap->target))
		return TRUE;

	if(active && (*(uint8_t*)vmap->val_ptr))
		return TRUE;

	gtk_switch_set_active(GTK_SWITCH(vmap->target),
			      (*(uint8_t*)vmap->val_ptr) ? 1 : 0);

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static gboolean
jmrg_switch_toggle(GtkSwitch *widget, gboolean state, gpointer data)
{
	uint8_t *val = (uint8_t*) data;
	(*val) = state ? 1 : 0;

	/* Let the default handler run */
	return FALSE;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_switch_init(const char* desc, int *val_ptr)
{
	GtkWidget *hbox = NULL;
	GtkWidget *label = NULL;
	GtkWidget *sw = NULL;
	struct value_map *vmap = NULL;

	/* But the switch and its label together
	 * in a horizontal box */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	if(!hbox)
		return NULL;

	sw = gtk_switch_new();
	if(!sw)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), sw, FALSE, FALSE, 6);

	label = gtk_label_new(desc);
	if(!label)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 6);


	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = sw;
	vmap->val_ptr = val_ptr;

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_switch_poll, vmap);

	g_signal_connect(sw, "state-set", G_CALLBACK(jmrg_switch_toggle),
			 val_ptr);

	g_signal_connect(sw, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return hbox;
 cleanup:
	if(sw)
		gtk_widget_destroy(sw);
	if(label)
		gtk_widget_destroy(label);
	if(hbox)
		gtk_widget_destroy(hbox);
	return NULL;
}
