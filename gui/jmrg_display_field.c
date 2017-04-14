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
		text = rds_get_ps(st);
		break;
	case RDS_FIELD_RT:
		text = rds_get_rt(st);
		break;
	case RDS_FIELD_PTYN:
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
	GtkWidget *hbox = NULL;
	GtkWidget *input = NULL;
	GtkWidget *flag_check = NULL;
	GtkWidget *set_button = NULL;
	GtkStyleContext *context = NULL;
	struct value_map *vmap = NULL;
	int has_flag = 1;
	int max_len = 1;

	switch(type) {
		case RDS_FIELD_PI:
			max_len = 5;
			has_flag = 0;
			break;
		case RDS_FIELD_PS:
			max_len = RDS_PS_LENGTH;
			has_flag = 0;
			break;
		case RDS_FIELD_RT:
			max_len = RDS_RT_LENGTH;
			has_flag = 1;
			break;
		case RDS_FIELD_PTYN:
			max_len = RDS_PTYN_LENGTH;
			has_flag = 0;
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


	/* Use a box to have better control */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);


	/* Create the display as a label widget */
	display  = gtk_label_new(NULL);
	if(!display)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), display, 1, 1, 6);
	gtk_label_set_max_width_chars(GTK_LABEL(display), max_len);

	/* Register custom CSS */
	context = gtk_widget_get_style_context(display);
	gtk_style_context_add_class(context,"rds_display_field");


	/* Create the input field with its set button and
	 * an optional checkbox (flag) for the RT field */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(vbox), hbox, 1, 1, 6);

	input = gtk_entry_new();
	if(!input)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), input, 1, 1, 6);
	gtk_entry_set_max_width_chars(GTK_ENTRY(input), max_len);

	if(has_flag) {
		flag_check = jmrg_checkbox_init(st, "Flush remote buffer",
						0, 0, 1);
		if(!flag_check)
			goto cleanup;
		gtk_box_pack_start(GTK_BOX(hbox), flag_check, 0, 0, 2);
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
	set_button = jmrg_set_button_init(st, "Set", vmap);
	if(!set_button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), set_button, 0, 0, 6);


	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_display_field_poll, vmap);

	g_signal_connect(display, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(vmap)
		free(vmap);
	if(input)
		gtk_widget_destroy(input);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(display)
		gtk_widget_destroy(display);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;	
}

