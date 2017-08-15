#include <gtk/gtk.h>	/* For GTK+ support */
#include "fmmod.h"	/* Also brings in rds_encoder.h and rtp_server.h */
#include "utils.h"

struct control_page {
	GtkWidget *container;
	GtkWidget *label;
	struct shm_mapping *shmem;
};

struct value_map {
	/* The widgets that polling function will update */
	GtkWidget *target;
	GtkWidget *target2;

	/* Widget from which to get value to set (for buttons) */
	GtkWidget *entry;

	/* Pointer of value to read/write to */
	void *val_ptr;

	/* Value associated with this mapping */
	int val;

	/* Event source id, used to remove
	 * polling loop from main loop */
	guint	esid;

	/* -== RTP-SPECIFIC ==- */
	struct rtp_server_control *rtp_ctl;
	GListStore *iplstore;

	/*-== RDS-SPECIFIC ==-*/
	struct rds_encoder_state *st;

	/* RDS field type associated with this widget */
	int type;

	/* Function pointers to bit field getters/setters
	 * used by checkboxes on RDSEnc panel */
	rds_bf_getter getter;
	rds_bf_setter setter;

	/* Bitmask to use when setting bitfields
	 * used when setting the various DI fields */
	int mask;

	/* Checkbox widget used for setting A/B flag on RT */
	GtkWidget *flag;

	/* Autocomplete entry match index */
	int acentry_match_idx;

	/* Dynamic PSN / RT infos */
	struct rds_dynps_state dps;
	struct rds_dynrt_state drt;

	/* Switch for disabling Dynamic PSN / RT */
	GtkWidget *sw;
};


GtkWidget* jmrg_switch_init(const char*, int *);
/* Widgets on FMMod panel */
GtkWidget* jmrg_mpx_plotter_init(int sample_rate, int max_samples);
GtkWidget* jmrg_vscale_init(const char*, float*, gdouble);
GtkWidget* jmrg_level_bar_init(const char*, float*);
GtkWidget* jmrg_radio_button_init(const char*, int *, int, GtkRadioButton*);
/* Widgets on RDSEnc panel */
GtkWidget* jmrg_set_button_init(const char*, struct value_map*);
GtkWidget* jmrg_file_chooser_init(struct value_map*);
GtkWidget* jmrg_checkbox_init(struct rds_encoder_state*, const char*,
			      int, int, int);
GtkWidget* jmrg_display_field_init(struct rds_encoder_state*, const char*, int);
GtkWidget* jmrg_cbox_text_init(struct rds_encoder_state*, const char*, int);
GtkWidget* jmrg_acentry_init(struct rds_encoder_state*, const char*, int);
/* Widgets on RTPServ panel */
GtkWidget* jmrg_iplist_init(struct rtp_server_control *ctl);
GtkWidget* jmrg_rtpstats_init(struct rtp_server_control *ctl);

/* Panels */
int jmrg_fmmod_panel_init(struct control_page*);
int jmrg_rdsenc_panel_init(struct control_page*);
int jmrg_rtpserv_panel_init(struct control_page *ctl_page);

/* Common signal handlers */
void jmrg_free_vmap(GtkWidget*, gpointer);
void jmrg_panel_destroy(GtkWidget*, struct control_page*);
