#include <stdlib.h> /* For malloc() / free() */
#include <string.h> /* For memset() */
#include "jmpxrds_gui.h"
#include "rds_codes.h"

/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_display_field_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rds_encoder_state *st = vmap->st;
	char pi[8] = {0};
	char *text = NULL;
	const char *def_ptyn = NULL;
	char ptyn[24] = {0};
	int tmp = 0;

	if(!GTK_IS_LABEL(vmap->target))
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	switch(vmap->type) {
	case RDS_FIELD_PI:
		tmp = rds_get_pi(st);
		snprintf(pi, 5, "%X", st->pi);
		gtk_label_set_text(GTK_LABEL(vmap->target), pi);
		return TRUE;
	case RDS_FIELD_PS:
		if(!st->ps_set)
			return TRUE;
		text = rds_get_ps(st);
		if(gtk_widget_get_sensitive(vmap->target2) == FALSE)
			gtk_widget_set_sensitive(vmap->target2, TRUE);
		break;
	case RDS_FIELD_RT:
		if(!st->rt_set)
			return TRUE;
		text = rds_get_rt(st);
		if(gtk_widget_get_sensitive(vmap->target2) == FALSE)
			gtk_widget_set_sensitive(vmap->target2, TRUE);
		break;
	case RDS_FIELD_PTYN:
		if(!st->ptyn_set)
			return TRUE;
		text = rds_get_ptyn(st);
		if(!text) {
			tmp = rds_get_pty(st);
			def_ptyn = rds_codes_get_pty_name(tmp);
			snprintf(ptyn, 24, "( %s )", def_ptyn);
			text = ptyn;
		}
		break;
	default:
		return FALSE;
	}

	if(text)
		gtk_label_set_text(GTK_LABEL(vmap->target), text);

	return TRUE;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_display_field_init(struct rds_encoder_state *st, const char* label, int type)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *display = NULL;
	GtkWidget *input_hbox = NULL;
	GtkWidget *input = NULL;
	GtkWidget *flag_check = NULL;
	GtkWidget *set_button = NULL;
	GtkWidget *file_chooser = NULL;
	GtkStyleContext *context = NULL;
	struct value_map *vmap = NULL;
	int has_flag = 0;
	int is_dynamic = 0;
	int max_len = 1;

	switch(type) {
		case RDS_FIELD_PI:
			max_len = 5;
			break;
		case RDS_FIELD_PS:
			max_len = RDS_PS_LENGTH;
			is_dynamic = 1;
			break;
		case RDS_FIELD_RT:
			max_len = RDS_RT_LENGTH;
			has_flag = 1;
			is_dynamic = 1;
			break;
		case RDS_FIELD_PTYN:
			max_len = RDS_PTYN_LENGTH;
			break;
		default:
			return NULL;
	};


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
	gtk_widget_set_valign(container, GTK_ALIGN_START);


	/* Use a box to have better control */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);

	/* Create the display as a label widget */
	display  = gtk_label_new(NULL);
	if(!display)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), display, 1, 0, 6);
	gtk_label_set_max_width_chars(GTK_LABEL(display), max_len);

	/* Register custom CSS */
	context = gtk_widget_get_style_context(display);
	gtk_style_context_add_class(context,"rds_display_field");


	/* Create the input field with its set button and
	 * an optional checkbox (flag) for the RT field */
	input_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!input_hbox)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), input_hbox, 1, 1, 6);

	input = gtk_entry_new();
	if(!input)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(input_hbox), input, 1, 1, 6);
	gtk_entry_set_max_width_chars(GTK_ENTRY(input), max_len);

	if(has_flag) {
		flag_check = jmrg_checkbox_init(st, "Flush remote buffer",
						0, 0, 1);
		if(!flag_check)
			goto cleanup;
		gtk_box_pack_start(GTK_BOX(input_hbox), flag_check, 0, 0, 2);
	}

	/* Initialize value_map */	
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->st = st;
	vmap->target = display;
	vmap->type = type;
	vmap->entry = input;
	vmap->flag = flag_check;

	/* Add the set button */
	set_button = jmrg_set_button_init("Set", vmap);
	if(!set_button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(input_hbox), set_button, 0, 0, 6);


	/* If it's Dynamic PSN / RT, add the file chooser */
	if(is_dynamic) {
		file_chooser = jmrg_file_chooser_init(vmap);
		if(!file_chooser)
			goto cleanup;
		gtk_box_pack_start(GTK_BOX(vbox), file_chooser, 1, 1, 6);
		gtk_widget_set_halign(file_chooser, GTK_ALIGN_START);
		gtk_widget_set_sensitive(file_chooser, FALSE);
		vmap->target2 = file_chooser;
	}

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_display_field_poll, vmap);

	g_signal_connect(display, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(vmap)
		free(vmap);
	if(set_button)
		gtk_widget_destroy(set_button);
	if(input)
		gtk_widget_destroy(input);
	if(input_hbox)
		gtk_widget_destroy(input_hbox);
	if(display)
		gtk_widget_destroy(display);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;	
}

