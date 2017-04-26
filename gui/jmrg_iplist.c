#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/**********************\
* IP ENTRY DECLARATION *
\**********************/

G_BEGIN_DECLS

#define JMRG_TYPE_IPENTRY (jmrg_ipentry_get_type ())
G_DECLARE_FINAL_TYPE(JMRG_IPEntry, jmrg_ipentry, JMRG, IPENTRY, GObject);

G_END_DECLS

struct _JMRG_IPEntry
{
	GObject		parent_instance;
	in_addr_t	addr;
};

G_DEFINE_TYPE(JMRG_IPEntry, jmrg_ipentry, G_TYPE_OBJECT);

void
jmrg_ipentry_init(JMRG_IPEntry* entry) {
	entry->addr = 0;
}

void
jmrg_ipentry_class_init(JMRG_IPEntryClass *class) {}

JMRG_IPEntry*
jmrg_ipentry_new(in_addr_t addr)
{
	JMRG_IPEntry* entry = NULL;
	entry = g_object_new(JMRG_TYPE_IPENTRY, NULL);

	if(entry)
		entry->addr = addr;

	return entry;
}


/*********\
* HELPERS *
\*********/

static GtkWidget *
jmrg_iplist_create_label(gpointer item, gpointer data)
{
	struct in_addr ipv4addr = { 0 };
	JMRG_IPEntry *entry = (JMRG_IPEntry*) item;
	GtkWidget *label = NULL;
	ipv4addr.s_addr = entry->addr;
	label = gtk_label_new(inet_ntoa(ipv4addr));
	return label;
}

static void
jmrg_iplist_remove(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rtp_server_control *ctl= vmap->rtp_ctl;
	struct in_addr ipv4addr = {0};
	union sigval value = {0};
	GtkWidget *label = gtk_bin_get_child(GTK_BIN(row));
	const char* label_text = gtk_label_get_text(GTK_LABEL(label));
	static int active_ips = 0;
	int i = 100;

	active_ips = ctl->num_receivers;
	inet_aton(label_text, &ipv4addr);
	value.sival_int = ipv4addr.s_addr;
	if(sigqueue(ctl->pid, SIGUSR2, value) != 0)
		utils_perr("Couldn't send signal, sigqueue()");

	/* Wait for it to get removed before moving to the next one */
	while(active_ips == ctl->num_receivers && i > 0) {
		usleep(200);
		i--;
	}

	return;
}


/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_iplist_poll(gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rtp_server_control *ctl= vmap->rtp_ctl;
	GtkListBoxRow *last_row = NULL;
	JMRG_IPEntry *entry = NULL;
	static int active_entries = 0;
	int i = 0;

	if(!ctl || !vmap->iplstore)
		return FALSE;

	if(!gtk_widget_is_visible(vmap->target))
		return TRUE;

	/* Don't update while removing IPs */
	if(vmap->val == -1)
		return TRUE;

	if(gtk_widget_has_focus(vmap->target) ||
	   active_entries == ctl->num_receivers)
		return TRUE;

	/* Update list store */
	g_list_store_remove_all(vmap->iplstore);
	for(i = 0; i < ctl->num_receivers; i++) {
		entry = jmrg_ipentry_new(ctl->receivers[i]);
		g_list_store_insert(vmap->iplstore, i, (gpointer) entry);
	}
	active_entries = ctl->num_receivers;

	/* Now update listbox's model binding */
	gtk_list_box_bind_model(GTK_LIST_BOX(vmap->target),
				G_LIST_MODEL(vmap->iplstore),
				jmrg_iplist_create_label,
				NULL, NULL);

	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_ipadd_button_clicked(GtkButton *button, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	struct rtp_server_control *ctl= vmap->rtp_ctl;
	struct in_addr ipv4addr = {0};
	union sigval value = {0};
	const char* ipentry = gtk_entry_get_text(GTK_ENTRY(vmap->entry));
	int ret = 0;

	ret = inet_aton(ipentry, &ipv4addr);
	if (!ret) {
		utils_err("Invalid IP address !\n");
		return;
	}

	value.sival_int = ipv4addr.s_addr;
	if (sigqueue(ctl->pid, SIGUSR1, value) != 0)
		utils_perr("Couldn't send signal, sigqueue()");

	return;
}

static void
jmrg_ipdel_button_clicked(GtkButton *button, gpointer data)
{
	struct value_map *vmap = (struct value_map*) data;
	vmap->val = -1;
	gtk_list_box_selected_foreach(GTK_LIST_BOX(vmap->target),
                               jmrg_iplist_remove,
                               data);
	vmap->val = 0;	
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_iplist_init(struct rtp_server_control *ctl)
{
	GtkWidget *container = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *listbox = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *add_button = NULL;
	GtkWidget *del_button = NULL;
	struct value_map *vmap = NULL;

	/* Use a frame to also have a label there
	 * for free */
	container = gtk_frame_new("RTP Targets (IP Addresses)");
	if(!container)
		goto cleanup;
	gtk_frame_set_label_align(GTK_FRAME(container), 0.5, 0.6);
	gtk_frame_set_shadow_type(GTK_FRAME(container),
				  GTK_SHADOW_ETCHED_IN);

	/* Use a box to have better control */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!vbox)
		goto cleanup;
	gtk_container_add(GTK_CONTAINER(container), vbox);

	/* The listbox that contains a label for each IP */
	listbox = gtk_list_box_new();
	if(!listbox)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(vbox), listbox, 1, 1, 6);
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_MULTIPLE);

	/* And the final row with the entry and the set button */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox)
		goto cleanup;
	gtk_box_pack_end(GTK_BOX(vbox), hbox, 0, 0, 6);

	entry = gtk_entry_new();
	if(!entry)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), entry, 1, 1, 6);

	add_button = gtk_button_new_with_label("Add");
	if(!add_button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), add_button, 0, 0, 6);

	del_button = gtk_button_new_with_label("Remove Selected");
	if(!del_button)
		goto cleanup;
	gtk_box_pack_start(GTK_BOX(hbox), del_button, 0, 0, 6);

	/* Initialize value_map */
	vmap = malloc(sizeof(struct value_map));
	if(!vmap)
		goto cleanup;
	memset(vmap, 0, sizeof(struct value_map));

	vmap->target = listbox;
	vmap->entry = entry;
	vmap->rtp_ctl = ctl;
	vmap->iplstore = g_list_store_new(JMRG_TYPE_IPENTRY);

	/* Register polling function and signal handlers */
	vmap->esid = g_timeout_add(200, jmrg_iplist_poll, vmap);


	g_signal_connect(add_button, "clicked",
			 G_CALLBACK(jmrg_ipadd_button_clicked),
			 vmap);

	g_signal_connect(del_button, "clicked",
			 G_CALLBACK(jmrg_ipdel_button_clicked),
			 vmap);

	g_signal_connect(container, "unrealize", G_CALLBACK(jmrg_free_vmap),
			 vmap);

	return container;
 cleanup:
	if(del_button)
		gtk_widget_destroy(del_button);
	if(add_button)
		gtk_widget_destroy(add_button);
	if(entry)
		gtk_widget_destroy(entry);
	if(hbox)
		gtk_widget_destroy(hbox);
	if(listbox)
		gtk_widget_destroy(listbox);
	if(vbox)
		gtk_widget_destroy(vbox);
	if(container)
		gtk_widget_destroy(container);
	return NULL;
}
