#include <stdlib.h>	/* For strtol() */
#include "jmpxrds_gui.h"
#include "rds_codes.h"

/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_set_button_clicked(__attribute__((unused)) GtkButton *button, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	const char *text = NULL;
	int flag = 0;
	int val = 0;
	int tmp = 0;

	switch(vmap->type) {
	case RDS_FIELD_PI:
		text = gtk_entry_get_text(GTK_ENTRY(vmap->entry));
		val = (unsigned int) strtol(text, NULL, 16);
		val &= 0xFFFF;
		rds_set_pi(st, (uint16_t) val);
		gtk_entry_set_text(GTK_ENTRY(vmap->entry), "");
		return;
	case RDS_FIELD_PS:
		text = gtk_entry_get_text(GTK_ENTRY(vmap->entry));
		rds_set_ps(st, text);
		gtk_entry_set_text(GTK_ENTRY(vmap->entry), "");
		return;
	case RDS_FIELD_RT:
		text = gtk_entry_get_text(GTK_ENTRY(vmap->entry));
		flag = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vmap->flag));
		rds_set_rt(st, text, flag);
		gtk_entry_set_text(GTK_ENTRY(vmap->entry), "");
		return;
	case RDS_FIELD_PTYN:
		text = gtk_entry_get_text(GTK_ENTRY(vmap->entry));
		rds_set_ptyn(st, text);
		gtk_entry_set_text(GTK_ENTRY(vmap->entry), "");
		return;
	case RDS_FIELD_ECC:
		val = vmap->acentry_match_idx;
		if(val < 0)
			return;
		tmp = rds_codes_get_ecc_by_ctry_idx(val);
		if(tmp < 0)
			return;
		rds_set_ecc(st, tmp);
		return;
	case RDS_FIELD_LIC:
		val = vmap->acentry_match_idx;
		if(val < 0)
			return;
		tmp = rds_codes_get_lic_by_lang_idx(val);
		if(tmp < 0)
			return;
		rds_set_lic(st, tmp);
		return;
	default:
		return;
	};

	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_set_button_init(const char* label, struct value_map *vmap)
{
	GtkWidget *button = NULL;

	if(!vmap)
		goto cleanup;

	button = gtk_button_new_with_label(label);
	if(!button)
		goto cleanup;

	g_signal_connect(button, "clicked",
			 G_CALLBACK(jmrg_set_button_clicked),
			 vmap);
	return button;
 cleanup:
	return NULL;
}
