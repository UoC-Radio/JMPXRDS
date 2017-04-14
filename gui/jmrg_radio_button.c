#include <stdlib.h>	/* For malloc() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_radio_button_toggle(GtkToggleButton *button, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	(*(int*)vmap->val_ptr) = vmap->val;
	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_radio_button_init(const char* label, int *val_ptr, int val,
		       GtkRadioButton* previous)
{
	GtkWidget *rbutton = NULL;
	struct value_map *vmap = NULL;

	rbutton = gtk_radio_button_new_with_label_from_widget(previous, label);
	if(!rbutton)
		goto cleanup;

	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		return NULL;
	memset(vmap, 0, sizeof(struct value_map));	
	vmap->val_ptr = val_ptr;
	vmap->val = val;

	/* Register signal hanlders */
	g_signal_connect(rbutton, "toggled",
			 G_CALLBACK(jmrg_radio_button_toggle),
			 vmap);

	g_signal_connect(rbutton, "unrealize",
			 G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return rbutton;
 cleanup:
	if(rbutton)
		gtk_widget_destroy(rbutton);
	return NULL;
}
