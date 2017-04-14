#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For strcmp() / memset() */
#include "jmpxrds_gui.h"
#include "rds_codes.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_acentry_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	int tmp = 0;
	int tmp2 = 0;
	const char* name = NULL;

	if(!GTK_IS_ENTRY(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	if(gtk_widget_has_focus(vmap->target))
		return TRUE;

	switch(vmap->type) {
	case RDS_FIELD_ECC:
		tmp = rds_get_ecc(st);
		tmp2 = rds_get_pi(st);
		tmp2 = (tmp2 >> 12);
		tmp = rds_codes_get_ctry_idx_from_ctry_codes(tmp2, tmp); 
		name = rds_codes_get_ctry_name(tmp);
		break;
	case RDS_FIELD_LIC:
		tmp = rds_get_lic(st);
		tmp = rds_codes_get_lang_idx_from_lic(tmp);
		name = rds_codes_get_lang_name(tmp);
		break;
	default:
		return FALSE;
	}

	gtk_entry_set_text(GTK_ENTRY(vmap->target), name);

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static gboolean
jmrg_acentry_match(GtkEntryCompletion *widget, GtkTreeModel *model,
		   GtkTreeIter *iter, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	GValue value = {0};

	gtk_tree_model_get_value(model, iter, 1, &value);
	vmap->acentry_match_idx = g_value_get_int(&value);

	g_value_unset(&value);

	/* Fallback to the default handler */
	return FALSE;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_acentry_init(struct rds_encoder_state *st, const char* label, int type)
{
	GtkWidget *container = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *acentry = NULL;
	GtkEntryCompletion *entry_completion = NULL;
	GtkWidget *set_button = NULL;
	GtkListStore *model = NULL;
	GtkTreeIter iter = {0};
	struct value_map *vmap = NULL;
	const char *name = NULL;
	const char *prev_name = NULL;
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
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), hbox);


	/* Initialize an entry with auto-completion */	
	acentry = gtk_entry_new();
	if(!acentry)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), acentry, 1, 1, 6);

	entry_completion = gtk_entry_completion_new();
	if(!entry_completion)
		goto cleanup;
	gtk_entry_set_completion(GTK_ENTRY(acentry), entry_completion);
	gtk_entry_completion_set_text_column(entry_completion, 0);


	/* Initialize auto-completion model */
	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	if(!model)
		goto cleanup;

	switch(type) {
	case RDS_FIELD_ECC:
		name = rds_codes_get_ctry_name(i);
		while(name[0] != '\0') {
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter, 0, name, 1, i, -1);
			prev_name = name;
			/* Some countries have multiple country codes
			 * and appear more than once, make sure we don't
			 * add their names again */
			while(!strcmp(prev_name, name)) {
				i++;
				name = rds_codes_get_ctry_name(i);
			}
		}
		break;
	case RDS_FIELD_LIC:
		name = rds_codes_get_lang_name(i);
		while(name[0] != '\0') {
			gtk_list_store_append(model, &iter);
			gtk_list_store_set(model, &iter, 0, name, 1, i, -1);
			name = rds_codes_get_lang_name(++i);
		}
		break;
	default:
		return NULL;
	}
	gtk_entry_completion_set_model(entry_completion, GTK_TREE_MODEL(model));


	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = acentry;
	vmap->st = st;
	vmap->type = type;
	vmap->acentry_match_idx = -1;


	/* Add the set button */
	set_button = jmrg_set_button_init(st, "Set", vmap);
	if(!set_button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), set_button, 1, 1, 6);


	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_acentry_poll, vmap);

	g_signal_connect(G_OBJECT(entry_completion), "match-selected",
			 G_CALLBACK(jmrg_acentry_match), vmap);

	g_signal_connect(acentry, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(vmap)
		free(vmap);
	if(model)
		g_object_unref(model);
	if(entry_completion)
		g_object_unref(entry_completion);
	if(acentry)
		gtk_widget_destroy(acentry);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
