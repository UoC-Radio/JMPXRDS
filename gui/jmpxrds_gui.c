#include <stdlib.h>	/* For malloc() / free() */
#include <time.h>	/* For nanosleep */
#include "jmpxrds_gui.h"

/******************\
* CUSTOM CSS STYLE *
\******************/

const char css_style[] = ".gain_bar block.empty, block.filled  {"\
				"min-height: 2px;"\
				"min-width: 8px;"\
				"border-style: solid;"\
				"border-color: black;"\
 			"}"\
			".rds_display_field {"\
				"background: #669999;"\
				"min-height: 50px;"\
				"color: black;"\
				"font-weight: bold;"\
				"font-family: monospace;"\
				"text-shadow: 1px 1px 5px black;"\
				"box-shadow: inset 0px 0px 5px black;"\
				"border: 1px solid black;"\
			"}";

/*********\
* HELPERS *
\*********/

static void
jmrg_hide_page_contents(struct control_page *page)
{
	GList *children = NULL;
	GList *current_child = NULL;

	children = gtk_container_get_children(GTK_CONTAINER(page->container));
	for(current_child = children; current_child != NULL;
	    current_child = g_list_next(current_child)) {
		gtk_widget_hide(GTK_WIDGET(current_child->data));
	}
	g_list_free(children);
}


/************************\
* COMMON SIGNAL HANDLERS *
\************************/

void
jmrg_free_vmap(GtkWidget *widget, gpointer data)
{
	const struct timespec tv = {0, 20000000L};
	struct value_map *vmap = (struct value_map*) data;
	if(vmap->esid) {
		g_source_remove(vmap->esid);
		/* Give it some time for the polling loop to run
		 * and terminate */
		nanosleep(&tv, NULL);
	}
	if(vmap->iplstore) {
		g_list_store_remove_all(vmap->iplstore);
		g_object_unref(vmap->iplstore);
	}
	free(vmap);
	return;
}

void
jmrg_panel_destroy(GtkWidget *container, struct control_page *ctl_page)
{
	if(ctl_page->shmem)
		utils_shm_destroy(ctl_page->shmem, 0);
	return;
}

/* When switching to a page, hide the widgets of the rest of the pages
 * so that we don't poll for their contents. Un-hide / show only the contents
 * of the current page. */
void
jmrg_panel_switched(GtkNotebook *notebook, GtkWidget *panel, guint page_no,
		    gpointer user_data)
{
	struct control_page **pages = (struct control_page **) user_data;
	int i = 0;
	gint no_pages = gtk_notebook_get_n_pages(notebook);

	for(i = 0; i < no_pages; i++) {
		if(i == page_no)
			gtk_widget_show_all(pages[i]->container);
		else
			jmrg_hide_page_contents(pages[i]);
	}

	return;
}


int
main(int argc, char *argv[])
{
	struct control_page *pages[3];
	GtkWidget *window = NULL;
	GtkWidget *notebook = NULL;
	GtkCssProvider *provider = NULL;
	GdkScreen *screen = NULL;
	struct control_page *fmmod_panel = NULL;
	struct control_page *rdsenc_panel = NULL;
	struct control_page *rtpserv_panel = NULL;
	int no_pages = 0;
	int ret = 0;

	/* Initialize gtk */
	gtk_init(&argc, &argv);

	/* Create top level window */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window) {
		ret = -1;
		goto cleanup;
	}
	gtk_window_set_title(GTK_WINDOW(window), "JMPXRDS Control Panel");
	gtk_window_set_default_size(GTK_WINDOW(window), -1, -1);
	/* Add event handler for closing the window */
	g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit),
			NULL);

	/* CSS stuff */
	provider = gtk_css_provider_new();
	if(!provider) {
		ret = -2;
		goto cleanup;
	}
	screen = gtk_widget_get_screen(window);
	gtk_style_context_add_provider_for_screen(screen,
					GTK_STYLE_PROVIDER(provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_css_provider_load_from_data(provider, css_style, -1, NULL);

	/* Create sub-container */
	notebook = gtk_notebook_new();
	if(!notebook) {
		ret = -3;
		goto cleanup;
	}
	gtk_container_add(GTK_CONTAINER(window), notebook);

	/* Initialize FMmod control panel */
	fmmod_panel = malloc(sizeof(struct control_page));
	if(!fmmod_panel) {
		ret = -4;
		goto cleanup;
	}
	ret = jmrg_fmmod_panel_init(fmmod_panel);
	if(ret < 0) {
		ret = -5;
		goto cleanup;
	}
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmmod_panel->container,
				 fmmod_panel->label);
	pages[no_pages++] = fmmod_panel;

	/* Initialize RDSEncoder control panel */
	rdsenc_panel = malloc(sizeof(struct control_page));
	if(!rdsenc_panel) {
		ret = -6;
		goto cleanup;
	}
	ret = jmrg_rdsenc_panel_init(rdsenc_panel);
	if(ret < 0) {
		ret = -7;
		goto cleanup;
	}
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), rdsenc_panel->container,
				 rdsenc_panel->label);
	pages[no_pages++] = rdsenc_panel;

	/* Initialize RTPServer control panel */
	rtpserv_panel = malloc(sizeof(struct control_page));
	if(!rtpserv_panel) {
		ret = -7;
		goto cleanup;
	}
	ret = jmrg_rtpserv_panel_init(rtpserv_panel);
	if(ret < 0) {
		ret = -7;
		goto cleanup;
	}
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), rtpserv_panel->container,
				 rtpserv_panel->label);
	pages[no_pages++] = rtpserv_panel;

	/* Register the signal handler for switching pages */
	g_signal_connect(notebook, "switch-page", G_CALLBACK(jmrg_panel_switched),
                         pages);

	gtk_widget_show_all(window);

	/* Manualy call the signal handler after the call to show_all() to hide
	 * the inactive pages. The currently displayed page is going to be the
	 * first one (0). */
	jmrg_panel_switched(GTK_NOTEBOOK(notebook), NULL, 0, pages);

	/* Start the gtk main loop */
	gtk_main();

 cleanup:
	return ret;
}
