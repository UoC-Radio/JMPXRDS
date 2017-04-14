#include <stdlib.h>	/* For malloc() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"
#include "rds_codes.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_cbox_text_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	int tmp = 0;

	if(!GTK_IS_COMBO_BOX(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	if(gtk_widget_has_focus(vmap->target))
		return TRUE;

	switch(vmap->type) {
	case RDS_FIELD_PTY:
		tmp = rds_get_pty(st);
		break;
	default:
		return FALSE;
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(vmap->target), tmp);

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_cbox_text_changed(GtkComboBox *cbox, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	int tmp = gtk_combo_box_get_active(cbox);

	switch(vmap->type) {
	case RDS_FIELD_PTY:
		rds_set_pty(st, (uint8_t) tmp);
		return;
	default:
		return;
	}

	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_cbox_text_init(struct rds_encoder_state *st, const char* label, int type)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *cbox = NULL;
	struct value_map *vmap = NULL;
	const char *pty_name = NULL;
	int i = 0;

	/* Use a frame to also have a label there
	 * for free */
	container = gtk_frame_new(label);
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.05, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);


	/* Use a box to have better control */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);


	/* Initialize combo box */
	cbox = gtk_combo_box_text_new();
	if(!cbox)
		goto cleanup;
	gtk_box_set_center_widget(GTK_BOX(vbox), cbox);
	switch(type) {
	case RDS_FIELD_PTY:
		pty_name = rds_codes_get_pty_name(i);
		while(pty_name[0] != '\0') {
			gtk_combo_box_text_insert(GTK_COMBO_BOX_TEXT(cbox), i,
						  NULL, pty_name);
			pty_name = rds_codes_get_pty_name(++i);
		}
		break;
	default:
		return NULL;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbox), 0);


	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = cbox;
	vmap->st = st;
	vmap->type = type;

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_cbox_text_poll, vmap);

	g_signal_connect(cbox, "changed", G_CALLBACK(jmrg_cbox_text_changed),
			 vmap);

	g_signal_connect(cbox, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(cbox)
		gtk_widget_destroy(cbox);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
