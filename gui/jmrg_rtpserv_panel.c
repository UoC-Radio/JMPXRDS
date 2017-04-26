#include <string.h>	/* For memset() */
#include "jmpxrds_gui.h"

/*************\
* ENTRY POINT *
\*************/

int
jmrg_rtpserv_panel_init(struct control_page *ctl_page)
{
	GtkWidget *hbox = NULL;
	GtkWidget *lvbox = NULL;
	GtkWidget *rvbox = NULL;
	GtkWidget *iplist = NULL;
	GtkWidget *rtpstats = NULL;
	struct rtp_server_control *ctl = NULL;
	int ret = 0;

	memset(ctl_page, 0, sizeof(struct control_page));

	/* Attach shared memory to talk with JMPXRDS */
	ctl_page->shmem = utils_shm_attach(RTP_SRV_SHM_NAME,
					   sizeof(struct rtp_server_control));
	if(!ctl_page->shmem) {
		utils_perr("Unable to communicate with JMPXRDS");
		ret = -1;
		goto cleanup;
	}
	ctl = (struct rtp_server_control*) ctl_page->shmem->mem;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(!hbox) {
		ret = -2;
		goto cleanup;
	}

	lvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!lvbox) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), lvbox, 1, 1, 6);

	iplist = jmrg_iplist_init(ctl);
	if(!iplist) {
		ret = -3;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(lvbox), iplist, 1, 1, 6);

	rvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	if(!rvbox) {
		ret = -4;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(hbox), rvbox, 0, 0, 6);

	rtpstats = jmrg_rtpstats_init(ctl);
	if(!rtpstats) {
		ret = -5;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(rvbox), rtpstats, 0, 0, 6);

	/* Set the label and container for the page */
	ctl_page->label = gtk_label_new("RTP Server");
	if(!ctl_page->label) {
		ret = -11;
		goto cleanup;
	}
	ctl_page->container = hbox;

	return 0;
 cleanup:
	return ret;
}

