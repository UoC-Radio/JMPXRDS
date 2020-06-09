if DEBUG
DEBUG_CFLAGS = -g --coverage
endif

bin_PROGRAMS = jmpxrds rds_tool fmmod_tool

jmpxrds_SOURCES = filters.c oscilator.c resampler.c rds_encoder.c \
		  rtp_server.c fmmod.c utils.c main.c
jmpxrds_LDADD = $(LIBM) $(LIBRT) $(LIBSAMPLERATE) $(LIBFFTW3F) $(LIBJACK)
jmpxrds_CFLAGS = $(CFLAGS) $(DEBUG_CFLAGS)

rds_tool_SOURCES = utils.c rds_config.c rds_dynpsrt.c rds_tool.c
rds_tool_LDADD = $(LIBRT)
rds_tool_LDFLAGS = -pthread
rds_tool_CFLAGS = $(CFLAGS) $(DEBUG_CFLAGS)

fmmod_tool_SOURCES = utils.c fmmod_tool.c
fmmod_tool_LDADD = $(LIBRT)
fmmod_tool_CFLAGS = $(CFLAGS) $(DEBUG_CFLAGS)

if GUI
bin_PROGRAMS += jmpxrds_gui

jmpxrds_gui_SOURCES = utils.c rds_config.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_file_chooser.c @srcdir@/rds_dynpsrt.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_radio_button.c @srcdir@/gui/jmrg_switch.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_level_bar.c @srcdir@/gui/jmrg_vscale.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_mpx_plotter.c @srcdir@/gui/jmrg_mpx_plotter_gl.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_checkbox.c @srcdir@/gui/jmrg_set_button.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_display_field.c @srcdir@/gui/jmrg_acentry.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_cbox_text.c @srcdir@/gui/jmrg_iplist.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_rtpstats.c @srcdir@/gui/jmrg_rtpserv_panel.c
jmpxrds_gui_SOURCES += @srcdir@/gui/jmrg_fmmod_panel.c @srcdir@/gui/jmrg_rdsenc_panel.c
jmpxrds_gui_SOURCES += @srcdir@/gui/rds_codes.c @srcdir@/gui/jmpxrds_gui.c

jmpxrds_gui_LDADD = $(LIBM) $(LIBRT) $(LIBFFTW3F) $(GTK_LIBS) $(Epoxy_LIBS)
jmpxrds_gui_CFLAGS = $(CFLAGS) $(GTK_CFLAGS) $(Epoxy_CFLAGS)
jmpxrds_gui_CFLAGS += -DDATA_PATH='"@datarootdir@/jmpxrds_gui/"'

# Install images
install-data-local:
	install -d -m 755 ${DESTDIR}@datarootdir@/jmpxrds_gui
	install -c -m 644 @srcdir@/gui/images/* ${DESTDIR}@datarootdir@/jmpxrds_gui/

uninstall-local:
	rm -rf ${DESTDIR}@datarootdir@/jmpxrds_gui
endif

if RTPSRV
bin_PROGRAMS += rtp_tool

rtp_tool_SOURCES = utils.c rtp_tool.c
rtp_tool_LDADD = $(LIBRT)
rtp_tool_CFLAGS = $(CFLAGS) $(GStreamer_CFLAGS) $(DEBUG_CFLAGS)

jmpxrds_LDADD += $(GStreamer_LIBS) $(LIBGSTAPP)
jmpxrds_CFLAGS += $(GStreamer_CFLAGS)

rds_tool_CFLAGS += $(GStreamer_CFLAGS)

fmmod_tool_CFLAGS += $(GStreamer_CFLAGS)

if GUI
jmpxrds_gui_CFLAGS += $(GStreamer_CFLAGS)
endif
endif

#Also clean up after autoconf
distclean-local:
	-rm -rf autom4te.cache
	-rm -rf build-aux
	-rm aclocal.m4
	-rm configure
	-rm *.in
	-rm *.gcda
	-rm *.gcno
	-rm *.gcov
	-rm gui/*.gcda
	-rm gui/*.gcno
	-rm gui/*.gcov
	-rm -rf ./.gcov_reports

if DEBUG
#Run tests and gather coverage data
test:
	-./test-run.sh
else
#Do nothing
test:
endif
