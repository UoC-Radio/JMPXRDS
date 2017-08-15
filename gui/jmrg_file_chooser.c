#include <stdlib.h>	/* For strtol() */
#include "jmpxrds_gui.h"
#include <linux/limits.h>	/* For PATH_MAX */
#include <string.h>		/* For strncmp() */

/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_file_chooser_done(GtkWidget *button, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	const char *filepath = NULL;
	int ret = 0;

	filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(button));

	switch(vmap->type) {
	case RDS_FIELD_PS:
		if((&vmap->dps)->active)
			rds_dynps_destroy(&vmap->dps);
		if(!filepath)
			return;
		ret = rds_dynps_init(&vmap->dps, vmap->st, filepath);
		break;
	case RDS_FIELD_RT:
		if((&vmap->drt)->active)
			rds_dynrt_destroy(&vmap->drt);
		if(!filepath)
			return;
		ret = rds_dynrt_init(&vmap->drt, vmap->st, filepath);
		break;
	default:
		return;
	};


	if(gtk_switch_get_state(GTK_SWITCH(vmap->sw)) == FALSE)
		gtk_switch_set_state(GTK_SWITCH(vmap->sw), TRUE);

	return;
}

static gboolean
jmrg_file_chooser_swtoggle(GtkSwitch *widget, gboolean state, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(widget));
	GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
	GtkWidget *file_chooser = g_list_nth_data(children, 1);
	int active = 0;

	switch(vmap->type) {
	case RDS_FIELD_PS:
		active = (&vmap->dps)->active;
		break;
	case RDS_FIELD_RT:
		active = (&vmap->drt)->active;
		break;
	default:
		return FALSE;
	};

	if(state) {
		if((&vmap->dps)->active)
			return FALSE;

		if(vmap->type == RDS_FIELD_PS && (&vmap->dps)->filepath)
			gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(file_chooser),
							 (&vmap->dps)->filepath);
		else if(vmap->type == RDS_FIELD_RT && (&vmap->drt)->filepath)
			gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(file_chooser),
							 (&vmap->drt)->filepath);
		else {
			gtk_switch_set_state(widget, FALSE);
			return FALSE;
		}
	} else {
		if(!(&vmap->dps)->active)
			return FALSE;
		gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(file_chooser));
	}

	g_signal_emit_by_name(G_OBJECT(file_chooser), "file-set");
	return FALSE;
}

/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_file_chooser_init(struct value_map *vmap)
{
	GtkWidget *container = NULL;
	GtkWidget *label = NULL;
	GtkWidget *button = NULL;
	GtkWidget *sw = NULL;
	const char* label_text;
	const char* chooser_title;

	if(!vmap)
		goto cleanup;

	container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!container)
		goto cleanup;

	switch(vmap->type) {
	case RDS_FIELD_PS:
		label_text = "Dynamic PSN from file";
		chooser_title = "Dynamic PSN data file";
		break;
	case RDS_FIELD_RT:
		label_text = "Dynamic RT from file";
		chooser_title = "Dynamic RT data file";
		break;
	}

	label = gtk_label_new(label_text);
	if(!label)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(container), label, 0, 0, 6);

	button = gtk_file_chooser_button_new(chooser_title,
					     GTK_FILE_CHOOSER_ACTION_OPEN);
	if(!button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(container), button, 0, 0, 6);

	sw = gtk_switch_new();
	if(!sw)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(container), sw, 0, 0, 8);

	vmap->sw = sw;

	g_signal_connect(sw, "state-set", G_CALLBACK(jmrg_file_chooser_swtoggle),
			 vmap);

	g_signal_connect(button, "file-set",
			 G_CALLBACK(jmrg_file_chooser_done),
			 vmap);
	return container;
 cleanup:
	if(button)
		gtk_widget_destroy(button);
	if(label)
		gtk_widget_destroy(label);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
