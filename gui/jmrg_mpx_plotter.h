#include "jmpxrds_gui.h" /* Also brings in gtk and utils */
#include <stdint.h>	/* For typed integers */
#include <fftw3.h>	/* For FFTW support */
#include <epoxy/gl.h>	/* For OpenGL support */

struct grid_point {
	GLfloat	x;
	GLfloat	y;
};

struct mpx_plotter {
	GtkWidget *glarea;
	GLuint	plot_program;
	GLuint	mh_program;
	GLuint	grid_program;
	GLint	f_color_mh;
	GLint	f_color_grid;
	GLfloat	*x_vals;
	GLfloat	*y_vals;
	GLfloat	*y_peak_vals;
	struct	grid_point *points;
	GLint	in_x_idx;
	GLint	in_y_idx;
	GLint	in_gp_idx;
	GLuint	vbos[3];
	GLuint	vao;
	fftw_complex *complex_buff;
	double	*real_buff;
	fftw_plan dft_plan;
	uint32_t sample_rate;
	uint16_t num_bins;
	uint16_t max_samples;
	uint16_t half_bins;
	uint16_t drawable_bins;
	float	*period;
	int	avg;
	int	max_hold;
	char	sockpath[32];
	guint	esid;
};


void jmrg_mpxp_gl_init(GtkGLArea*, gpointer);
void jmrg_mpxp_gl_destroy(GtkGLArea*, struct mpx_plotter*);
gboolean jmrg_mpxp_gl_render(GtkGLArea*, GdkGLContext *, gpointer);

