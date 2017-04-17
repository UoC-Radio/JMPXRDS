#include <stdlib.h>	/* For malloc() / free() */
#include <string.h>	/* For memset() */
#include <math.h>	/* For pow() / sqrt() / log10() */
#include "jmrg_mpx_plotter.h"

/*********\
* HELPERS *
\*********/

static int
jmrg_mpxp_create_grid_points(struct mpx_plotter *mpxp)
{
	float div = 2.0 / 12.0;
	int i = 0;
	float j = 0;

	/* Horizonal lines -> 5KHz steps
	 * Vertical lines -> 5dB steps
	 * Since we have 60KHz on x and 60dB on y we 'll need
	 * 12 horizonal and vertical lines, since each line needs
	 * 2 points we need 2 * 2 * 12 = 48 points */
	mpxp->points = malloc(48 * sizeof(struct grid_point));
	if(!mpxp->points)
		return -1;

	/* Horizonal lines: (-1,div) - (1, div) */
	for(i = 0, j = -1.0; i < 24; i++, j+= div) {
		mpxp->points[i].x = -1.0;
		mpxp->points[i].y = j;

		i++;

		mpxp->points[i].x = 1.0;
		mpxp->points[i].y = j;
	}

	/* Vertical lines: (div, -1) - (div, 1) */
	for(i = 24, j = -1.0; i < 48; i++, j+= div) {
		mpxp->points[i].x = j;
		mpxp->points[i].y = -1.0;

		i++;

		mpxp->points[i].x = j;
		mpxp->points[i].y = 1.0;
	}

	return 0;
}


/***************\
* DATA HANDLING *
\***************/

static void
jmrg_mpxp_update_y_vals(struct mpx_plotter *mpxp)
{
	float norm = 1.0 / (float) mpxp->num_bins;
	float mag = 0.0;
	float db = 0.0;
	float scaled = 0.0;
	float out = -1.0;
	int i = 0;
	int ret = 0;
	static int skip = 0;
	FILE	*sock;

	/* Got an underrun, give it some time
	 * to recover */
	if(skip) {
		skip = 0;
		return;
	}

	sock = fopen(mpxp->sockpath, "rb");
	if(sock == NULL) {
		perror("fopen()");
		return;
	}

	ret = fread((void*) mpxp->real_buff, sizeof(float),
		    mpxp->max_samples, sock);
	if(ret != mpxp->max_samples) {
		perror("fread()");
		skip = 1;
		return;
	}

	fclose(sock);

	fftwf_execute(mpxp->dft_plan);

	for(i = 0; i < mpxp->drawable_bins; i++) {
		mag = powf(mpxp->complex_buff[i][0], 2) +
		      powf(mpxp->complex_buff[i][1], 2);
		mag = sqrtf(mag) * norm;
		db = 20 * log10f(mag);
		/* We want a range from -60 to 0, so -60 -> -1, 0 -> 1 */
		if(db <= -60)
			scaled = -1;
		else if(db >= 0)
			scaled = 1;
		else
			scaled = (db + 30.0L) / 30.0L;

		if(mpxp->avg) {
			mpxp->y_vals[i] -= 0.008;
			if(scaled > mpxp->y_vals[i])
				mpxp->y_vals[i] = scaled;
		} else
			mpxp->y_vals[i] = scaled;

		if(scaled > mpxp->y_peak_vals[i])
			mpxp->y_peak_vals[i] = scaled;
	}
	return;
}


/******************\
* POLLING FUNCTION *
\******************/

static gboolean
jmrg_mpxp_redraw(gpointer data)
{
	struct mpx_plotter *mpxp = (struct mpx_plotter*) data;

	if(!gtk_widget_is_visible(mpxp->glarea))
		return TRUE;

	jmrg_mpxp_update_y_vals(mpxp);

	gtk_widget_queue_draw(mpxp->glarea);
	return TRUE;
}


/*****************\
* SIGNAL HANDLERS *
\*****************/

static void
jmrg_mpxp_toggle_avg(GtkToggleButton *togglebutton, gpointer data)
{
	struct mpx_plotter *mpxp = (struct mpx_plotter*) data;
	mpxp->avg = (mpxp->avg) ? 0 : 1;
	return;
}

static void
jmrg_mpxp_toggle_mh(GtkToggleButton *togglebutton, gpointer data)
{
	struct mpx_plotter *mpxp = (struct mpx_plotter*) data;
	int i = 0;

	mpxp->max_hold = (mpxp->max_hold) ? 0 : 1;

	for(i = 0; i < mpxp->drawable_bins; i++)
		mpxp->y_peak_vals[i] = -1.0;

	return;
}

static void
jmrg_mpxp_destroy(GtkWidget *container, struct mpx_plotter *mpxp)
{
	const struct timespec tv = {0, 83000000L};

	if(!mpxp)
		return;

	g_source_remove(mpxp->esid);
	nanosleep(&tv, NULL);

	jmrg_mpxp_gl_destroy(GTK_GL_AREA(mpxp->glarea), mpxp);

	if(mpxp->x_vals)
		free(mpxp->x_vals);
	if(mpxp->y_vals)
		free(mpxp->y_vals);
	if(mpxp->y_peak_vals)
		free(mpxp->y_peak_vals);
	if(mpxp->points)
		free(mpxp->points);
	if(mpxp->complex_buff)
		fftwf_free(mpxp->complex_buff);
	if(mpxp->real_buff)
		fftwf_free(mpxp->real_buff);
	free(mpxp);

	return;
}


/*************\
* ENTRY POINT *
\*************/

GtkWidget*
jmrg_mpx_plotter_init(int sample_rate, int max_samples)
{
	GtkWidget *container = NULL;
	GtkWidget *glarea = NULL;
	GtkWidget *button_box = NULL;
	GtkWidget *avg_tbutton = NULL;
	GtkWidget *mh_tbutton = NULL;
	uint32_t nyquist_freq = 0;
	double passband_ratio = 0.0;
	int middle_point = 0;
	struct mpx_plotter *mpxp = NULL;
	int ret = 0;
	int i = 0;

	mpxp = malloc(sizeof(struct mpx_plotter));
	if(!mpxp) {
		ret = -1;
		goto cleanup;
	}
	memset(mpxp, 0, sizeof(struct mpx_plotter));

	/* Prepare socket's path for reading samples */
	snprintf(mpxp->sockpath, 32, "/run/user/%i/jmpxrds.sock", getuid());

	/* Initialize default state */
	mpxp->sample_rate = sample_rate;
	mpxp->max_samples = max_samples;
	mpxp->num_bins = max_samples;
	mpxp->half_bins = (mpxp->num_bins / 2) + 1;
	mpxp->max_hold = 0;
	mpxp->avg = 0;

	/* We only want to draw up to 60KHz, half_bins
	 * contain the whole spectrum up to nyquist_freq */
	nyquist_freq = sample_rate / 2;
	passband_ratio = (double) 60000.0L / (double) nyquist_freq;
	mpxp->drawable_bins = (uint16_t) (passband_ratio * (double) mpxp->half_bins);

	/* Our plot can't be smaller than 120x120 since the grid won't fit */
	if(mpxp->drawable_bins < 120) {
		ret = -1;
		goto cleanup;
	}

	/* Allocate buffers */
	mpxp->real_buff = fftwf_alloc_real(mpxp->max_samples);
	if(!mpxp->real_buff) {
		ret = -3;
		goto cleanup;
	}

	mpxp->complex_buff = fftwf_alloc_complex(mpxp->half_bins);
	if(!mpxp->complex_buff) {
		ret = -4;
		goto cleanup;
	}

	mpxp->x_vals = malloc(mpxp->drawable_bins * sizeof(float));
	if(!mpxp->x_vals) {
		ret = -5;
		goto cleanup;
	}

	mpxp->y_vals = malloc(mpxp->drawable_bins * sizeof(float));
	if(!mpxp->y_vals) {
		ret = -6;
		goto cleanup;
	}
	for(i = 0; i < mpxp->drawable_bins; i++)
		mpxp->y_vals[i] = -1.0;

	mpxp->y_peak_vals = malloc(mpxp->drawable_bins * sizeof(float));
	if(!mpxp->y_peak_vals) {
		ret = -7;
		goto cleanup;
	}
	for(i = 0; i < mpxp->drawable_bins; i++)
		mpxp->y_peak_vals[i] = -1.0;

	/* Create DFT plan */
	mpxp->dft_plan = fftwf_plan_dft_r2c_1d(mpxp->num_bins, mpxp->real_buff,
					     mpxp->complex_buff, FFTW_MEASURE);
	if(!mpxp->dft_plan) {
		ret = -8;
		goto cleanup;
	}

	/* Initialize x axis and grid points */
	/* Window's coordinates go from -1.0 to +1.0 */
	middle_point = (mpxp->drawable_bins + 1) / 2;
	for(i = 0; i < mpxp->drawable_bins; i++)
		mpxp->x_vals[i] = (float)(i - middle_point) / (float)middle_point;

	ret = jmrg_mpxp_create_grid_points(mpxp);
	if(ret < 0) {
		ret = -9;
		goto cleanup;
	}


	/* Create the top level container */
	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	if(!container) {
		ret = -10;
		goto cleanup; 
	}


	/* Create the GtkGLArea and pack it on the box */
	glarea = gtk_gl_area_new();
	if(!glarea) {
		ret = -11;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), glarea, 1, 1, 2);
	mpxp->glarea = glarea;


	/* Create a new horizonal box for the buttons */
	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	if(!button_box) {
		ret = -12;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(container), button_box, 0, 0, 0);

	/* Create the two toggle buttons, one for smoothing and one for
	 * max hold */
	avg_tbutton = gtk_toggle_button_new_with_label("Smooth Hold");
	if(!avg_tbutton) {
		ret = -13;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(button_box), avg_tbutton, 1, 1, 2);

	mh_tbutton = gtk_toggle_button_new_with_label("Max Hold");
	if(!mh_tbutton) {
		ret = -14;
		goto cleanup;
	}
	gtk_box_pack_start(GTK_BOX(button_box), mh_tbutton, 1, 1, 2);


	/* Register polling function and signal handlers */

	/* 12FPS -> (1 / 12 * 1000)ms for each frame */
	mpxp->esid = g_timeout_add(83, jmrg_mpxp_redraw, mpxp);

	/* GL Area */
	g_signal_connect(glarea, "realize", G_CALLBACK(jmrg_mpxp_gl_init), mpxp);
	g_signal_connect(glarea, "render", G_CALLBACK(jmrg_mpxp_gl_render), mpxp);

	/* Buttons */
	g_signal_connect(avg_tbutton, "toggled", G_CALLBACK(jmrg_mpxp_toggle_avg), mpxp);
	g_signal_connect(mh_tbutton, "toggled", G_CALLBACK(jmrg_mpxp_toggle_mh), mpxp);

	/* Now register mpxp_destroy as the callback for top level container's
	 * unrealize event */
	g_signal_connect(container, "unrealize", G_CALLBACK(jmrg_mpxp_destroy),
			mpxp);

	return container;
 cleanup:
	if(avg_tbutton)
		gtk_widget_destroy(avg_tbutton);
	if(button_box)
		gtk_widget_destroy(button_box);
	if(glarea)
		gtk_widget_destroy(glarea);
	if(container)
		gtk_widget_destroy(container);
	jmrg_mpxp_destroy(NULL, mpxp);
	utils_err("[MPX PLOTTER] Init failed with code: %i\n", ret);
	return NULL;
}
