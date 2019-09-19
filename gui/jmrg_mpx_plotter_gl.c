#include "jmrg_mpx_plotter.h"

/*********\
* SHADERS *
\*********/

const char *plot_vs_source =
	"#version 120\n"
	"attribute float in_x;"
	"attribute float in_y;"
	"varying vec4 f_color;"
	"void main(void) {"
	"  gl_Position = vec4(in_x, in_y, 0.0, 1.0);"
	"  f_color = vec4(in_y / 2 + 0.5, 0.4, 0.4, 1);"
	"}";

const char *plot_fs_source =
	"#version 120\n"
	"varying vec4 f_color;"
	"void main(void) {"
	"  gl_FragColor = f_color;"
	"}";

const char *mh_vs_source =
	"#version 120\n"
	"attribute float in_x;"
	"attribute float in_y;"
	"void main(void) {"
	"  gl_Position = vec4(in_x, in_y, 0.0, 1.0);"
	"}";


const char *grid_vs_source =
	"#version 120\n"
	"attribute vec2 in_gp;"
	"void main(void) {"
	"  gl_Position = vec4(in_gp.xy, 0.0, 1.0);"
	"}";

const char *common_fs_source =
	"#version 120\n"
	"uniform vec4 f_color;"
	"void main(void) {"
	"  gl_FragColor = f_color;"
	"}";

enum gl_programs {
	GLP_PLOT = 0,
	GLP_MAX_HOLD = 1,
	GLP_GRID = 2,
};


/***********************\
* OPENGL INITIALIZATION *
\***********************/

static GLuint
jmrg_mpxp_gl_program_init(struct mpx_plotter *mpxp, int type)
{
	GLuint program = GL_FALSE;
	GLint compile_ok = GL_FALSE;
	GLint link_ok = GL_FALSE;
	GLuint vs = GL_FALSE;
	GLuint fs = GL_FALSE;
	int ret = 0;
	const char* vs_source;
	const char* fs_source;

	switch(type) {
	case GLP_PLOT:
		vs_source = plot_vs_source;
		fs_source = plot_fs_source;
		break;
	case GLP_MAX_HOLD:
		vs_source = mh_vs_source;
		fs_source = common_fs_source;
		break;
	case GLP_GRID:
		vs_source = grid_vs_source;
		fs_source = common_fs_source;
		break;
	default:
		return -1;
	}

	/* Create vertex shader */	
	vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vs_source, NULL);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &compile_ok);
	if(!compile_ok) {
		ret = -2;
		goto cleanup;
	}

	/* Create fragment shader */
	fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fs_source, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &compile_ok);
	if(!compile_ok) {
		ret = -3;
		goto cleanup;
	}

	/* Create a program and attach the shaders on it */
	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);

	/* Bind attribute indices */
	mpxp->in_x_idx = 0;
	mpxp->in_y_idx = 1;
	mpxp->in_gp_idx = 2;

	switch(type) {
	case GLP_MAX_HOLD:
	case GLP_PLOT:
		glBindAttribLocation(program, mpxp->in_x_idx, "in_x");
		glBindAttribLocation(program, mpxp->in_y_idx, "in_y");
		break;
	case GLP_GRID:
		glBindAttribLocation(program, mpxp->in_gp_idx, "in_gp");
		break;
	default:
		return -1;
	}

	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
	if(!link_ok) {
		ret = -4;
		goto cleanup;
	}

 cleanup:
	/* the individual shaders can be detached and destroyed */
	if(vs) {
		glDetachShader(program, vs);
		glDeleteShader(vs);
	}
	if(fs) {
		glDetachShader(program, fs);
		glDeleteShader(vs);
	}
	if(ret < 0) {
		if(link_ok)
			glDeleteProgram(program);
		return (GLuint) ret;
	}
	return program;
}

static int
jmrg_mpxp_gl_buffers_init(struct mpx_plotter *mpxp)
{
	/* Init VAO and bind it as the currently used object */
	glGenVertexArrays(1, &mpxp->vao);
	glBindVertexArray(mpxp->vao);

	/* Init VBO buffers for x_vals, y_vals
	 * and grid points */
	glGenBuffers(3, mpxp->vbos);

	/* Initialize x axis for plot */

	/* Mark the first vbo as being the active one */
	glBindBuffer(GL_ARRAY_BUFFER, mpxp->vbos[0]);

	/* Copy x_vals array to the vbo, x coords
	 * for the plot are static, so is the vbo  */
	glBufferData(GL_ARRAY_BUFFER, mpxp->drawable_bins * sizeof(float),
		     mpxp->x_vals, GL_STATIC_DRAW);

	/* Specify that the above data goes to in_x attribute
	 * as a 1d float array */
	glVertexAttribPointer(mpxp->in_x_idx, 1,
			      GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);


	/* Prepare the y buffer for the plot */

	/* Mark the second vbo as being the active one */
	glBindBuffer(GL_ARRAY_BUFFER, mpxp->vbos[1]);

	/* Allocate a buffer with no data and mark it as dynamic
	 * since its's going to be updated frequently */
	glBufferData(GL_ARRAY_BUFFER, mpxp->drawable_bins * sizeof(float),
		     NULL, GL_DYNAMIC_DRAW);

	/* Same as above */
	glVertexAttribPointer(mpxp->in_y_idx, 1,
			      GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);

	/* Initialize buffer for grid points */
	glBindBuffer(GL_ARRAY_BUFFER, mpxp->vbos[2]);
	glBufferData(GL_ARRAY_BUFFER, 48 * sizeof(struct grid_point),
		     mpxp->points, GL_STATIC_DRAW);
	glVertexAttribPointer(mpxp->in_gp_idx, 2,	/* 2d array */
			      GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);

	/* Reset state */
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return 0;
}

void
jmrg_mpxp_gl_destroy(GtkGLArea *area, struct mpx_plotter *mpxp)
{
	/* Switch the current context to area's context
	 * in order to be able to call the GL API */
	gtk_gl_area_make_current(area);

	if(gtk_gl_area_get_error(area) != NULL)
		return;

	glDisableVertexAttribArray(mpxp->in_gp_idx);
	glDisableVertexAttribArray(mpxp->in_x_idx);
	glDisableVertexAttribArray(mpxp->in_y_idx);
	glDeleteProgram(mpxp->plot_program);
	glDeleteProgram(mpxp->mh_program);
	glDeleteProgram(mpxp->grid_program);
	glDeleteBuffers(3, mpxp->vbos);
	glDeleteVertexArrays(1, &mpxp->vao);
}

void
jmrg_mpxp_gl_init(GtkGLArea *area, gpointer data)
{
	GError *err = NULL;
	GLint ret = 0;
	struct mpx_plotter *mpxp = (struct mpx_plotter*) data;

	/* Switch the current context to area's context
	 * in order to be able to call the GL API */
	gtk_gl_area_make_current(area);

	if(gtk_gl_area_get_error(area) != NULL)
		return;

	ret = jmrg_mpxp_gl_program_init(mpxp, GLP_PLOT);
	if(ret < 0) {
		err = g_error_new(1, (int) ret, "GLSL setup failed for PLOT"
				  " with code: %i\n", (int) ret);
		gtk_gl_area_set_error(area, err);
		g_error_free(err);
		return;
	} else
		mpxp->plot_program = ret;

	ret = jmrg_mpxp_gl_program_init(mpxp, GLP_MAX_HOLD);
	if(ret < 0) {
		err = g_error_new(1, (int) ret, "GLSL setup failed for MAX_HOLD"
				  " with code: %i\n", (int) ret);
		gtk_gl_area_set_error(area, err);
		g_error_free (err);
		return;
	} else
		mpxp->mh_program = ret;

	mpxp->f_color_mh = glGetUniformLocation(mpxp->mh_program, "f_color");
	if(mpxp->f_color_mh == -1) {
		err = g_error_new(1, -1, "Could not bind f_color_mh\n");
		gtk_gl_area_set_error(area, err);
		g_error_free(err);
		return;
	}

	ret = jmrg_mpxp_gl_program_init(mpxp, GLP_GRID);
	if(ret < 0) {
		err = g_error_new(1, (int) ret, "GLSL setup failed for GRID"
				  " with code: %i\n", (int) ret);
		gtk_gl_area_set_error(area, err);
		g_error_free(err);
		return;
	} else
		mpxp->grid_program = ret;

	mpxp->f_color_grid = glGetUniformLocation(mpxp->grid_program, "f_color");
	if(mpxp->f_color_grid == -1) {
		err = g_error_new(1, -1, "Could not bind f_color_grid\n");
		gtk_gl_area_set_error(area, err);
		g_error_free(err);
		return;
	}

	ret = jmrg_mpxp_gl_buffers_init(mpxp);
	if(ret < 0) {
		err = g_error_new(1, (int) ret, "Buffer initialization failed"
				  "with code: %i\n", (int) ret);
		gtk_gl_area_set_error(area, err);
		g_error_free(err);
		return;
	}
}


/******************\
* OPENGL RENDERING *
\******************/

static void
jmrg_mpxp_gl_draw_grid(struct mpx_plotter *mpxp)
{
	GLfloat blueish[4] = {0.2, 0.0, 0.6, 1 };

	/* Load program */
	glUseProgram(mpxp->grid_program);

	/* Bind VAO */
	glBindVertexArray(mpxp->vao);

	/* Set the color */
	glUniform4fv(mpxp->f_color_grid, 1, blueish);

	/* Enable usage of the in_gp attribute */
	glEnableVertexAttribArray(mpxp->in_gp_idx);

	/* Draw lines between pairs of points */
	glDrawArrays(GL_LINES, 0, 48);

	/* Done, reset state */
	glDisableVertexAttribArray(mpxp->in_gp_idx);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

static void
jmrg_mpxp_gl_draw_plot(struct mpx_plotter *mpxp)
{
	/* Load program */
	glUseProgram(mpxp->plot_program);

	/* Bind VAO */
	glBindVertexArray(mpxp->vao);

	/* Enable usage of the in_x and in_y attributes */
	glEnableVertexAttribArray(mpxp->in_x_idx);
	glEnableVertexAttribArray(mpxp->in_y_idx);

	/* Overwrite the vbo with the updated y vals */
	glBindBuffer(GL_ARRAY_BUFFER, mpxp->vbos[1]);
	glBufferSubData(GL_ARRAY_BUFFER, 0, mpxp->drawable_bins *
			sizeof(float), mpxp->y_vals);

	/* Draw a line that connects all points */
	glDrawArrays(GL_LINE_STRIP, 0, mpxp->drawable_bins);

	/* Done, reset state */
	glDisableVertexAttribArray(mpxp->in_x_idx);
	glDisableVertexAttribArray(mpxp->in_y_idx);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

static void
jmrg_mpxp_gl_draw_peaks(struct mpx_plotter *mpxp)
{
	GLfloat greenish[4] = { 0.0, 0.5, 0.0, 1 };

	/* Load program */
	glUseProgram(mpxp->mh_program);

	/* Bind VAO */
	glBindVertexArray(mpxp->vao);

	/* Set the color */
	glUniform4fv(mpxp->f_color_mh, 1, greenish);

	/* Enable usage of the in_x and in_y attributes */
	glEnableVertexAttribArray(mpxp->in_x_idx);
	glEnableVertexAttribArray(mpxp->in_y_idx);

	/* Overwrite the vbo with y_peak_vals */
	glBindBuffer(GL_ARRAY_BUFFER, mpxp->vbos[1]);
	glBufferSubData(GL_ARRAY_BUFFER, 0, mpxp->drawable_bins *
			sizeof(float), mpxp->y_peak_vals);

	/* Draw a line that connects all points */
	glDrawArrays(GL_LINE_STRIP, 0, mpxp->drawable_bins);

	/* Done, reset state */
	glDisableVertexAttribArray(mpxp->in_x_idx);
	glDisableVertexAttribArray(mpxp->in_y_idx);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

gboolean
jmrg_mpxp_gl_render(__attribute__((unused)) GtkGLArea *area,
		    __attribute__((unused)) GdkGLContext *context,
		    gpointer data)
{
	struct mpx_plotter *mpxp = (struct mpx_plotter*) data;

	/* context and viewport already set from above */

	/* Clear up the buffer with black color */
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	jmrg_mpxp_gl_draw_grid(mpxp);

	jmrg_mpxp_gl_draw_plot(mpxp);

	if(mpxp->max_hold)
		jmrg_mpxp_gl_draw_peaks(mpxp);

	glFlush();

	return TRUE;
}
