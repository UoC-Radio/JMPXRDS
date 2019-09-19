#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_checkbox_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	uint8_t tmp = 0;
	uint8_t active = 0;

	if(!GTK_IS_TOGGLE_BUTTON(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	if(gtk_widget_has_focus(vmap->target))
		return TRUE;

	tmp = vmap->getter(st);
	tmp &= vmap->mask;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vmap->target));

	if((!tmp && !active) || (tmp && active))
		return TRUE;

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vmap->target),
				     (tmp != 0) ? TRUE : FALSE);
	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_checkbox_toggled(GtkToggleButton *tbutton, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	uint8_t tmp = 0;
	uint8_t active = 0;

	tmp = vmap->getter(st);

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tbutton));

	if((!(tmp & vmap->mask) && !active) || ((tmp & vmap->mask) && active))
		return;

	if(active)
		vmap->setter(st, tmp | vmap->mask);
	else
		vmap->setter(st, tmp & ~(vmap->mask));

	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_checkbox_init(struct rds_encoder_state *st, const char* label, int type,
		   int mask, int ignore)
{
	GtkWidget *checkbox = NULL;
	struct value_map *vmap = NULL;

	/* Initialize checkbox */
	checkbox = gtk_check_button_new_with_label(label);
	if(!checkbox)
		goto cleanup;

	/* Checkbox is managed from someone else (used for RT A/B flag) */
	if(ignore)
		return checkbox;


	/* Initialize value_map */
	vmap = (struct value_map*) malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = checkbox;
	vmap->st = st;
	vmap->mask = mask;

	switch(type) {
	case RDS_FIELD_TA:
		vmap->getter = rds_get_ta;
		vmap->setter = rds_set_ta;
		break;
	case RDS_FIELD_TP:
		vmap->getter = rds_get_tp;
		vmap->setter = rds_set_tp;
		break;
	case RDS_FIELD_MS:
		vmap->getter = rds_get_ms;
		vmap->setter = rds_set_ms;
		break;
	case RDS_FIELD_DI:
		vmap->getter = rds_get_di;
		vmap->setter = rds_set_di;
		break;
	default:
		goto cleanup;
	}


	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_checkbox_poll, vmap);

	g_signal_connect(checkbox, "toggled", G_CALLBACK(jmrg_checkbox_toggled),
			 vmap);

	g_signal_connect(checkbox, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return checkbox;
 cleanup:
	if(vmap)
		free(vmap);
	if(checkbox)
		gtk_widget_destroy(checkbox);
	return NULL;
}
