#Prelude
AC_INIT([jmpxrds],[0.5],[mickflemm+jmpxrds@gmail.com])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror dist-bzip2])

#Configuration / define macros
AC_ARG_WITH([wave-table],
	    AS_HELP_STRING([--with-wave-table],
			   [Use a wave table for the oscilators]),
	    AC_DEFINE([USE_WAVE_TABLE],[1],
		      [Define this to use wave table for the oscilators]),
	    [])
AC_DEFINE([USE_CUBIC_INTERPOLATION],[1],
	  [Define this to use cubic interpolation on the wave table(default)])

#AC_DEFINE([USE_LINEAR_INTERPOLATION], [0],
#	  [Define this to use linear interpolation on the wave table (not good)])
AC_ARG_WITH([rtp-server],
	    AS_HELP_STRING([--without-rtp-server],
			   [Don 't build the embedded RTP server]),
	    [],
	    [with_rtp_server=yes])

AC_ARG_WITH([gtk],
	    AS_HELP_STRING([--without-gtk],
			   [Don't build the GUI]),
	    [],
	    [with_gtk=yes])

# Check for programs
AC_PROG_CC

# Check for libraries
AC_CHECK_LIB([m], [sin], [LIBM=-lm],
	     AC_MSG_ERROR([Could not find libm]))
AC_SUBST([LIBM])
AC_CHECK_LIB([rt], [shm_open], [LIBRT=-lrt],
	     AC_MSG_ERROR([Could not find librt]))
AC_SUBST([LIBRT])
AC_CHECK_LIB([soxr], [soxr_create], [LIBSAMPLERATE=-lsoxr],
	     AC_MSG_ERROR([Could not find libsoxr]))
AC_SUBST([LIBSAMPLERATE])
AC_CHECK_LIB([fftw3], [fftw_execute], [LIBFFTW3=-lfftw3],
	     AC_MSG_ERROR([Could not find libfftw3]))
AC_SUBST([LIBFFTW3])
AC_CHECK_LIB([jack], [jack_client_open], [LIBJACK=-ljack],
	     AC_MSG_ERROR([Could not find jack libraries]))
AC_SUBST([LIBJACK])

AS_IF([test "x$with_rtp_server" != xno],
	[PKG_CHECK_MODULES(GStreamer,
			   [
				gstreamer-1.0 >= 1.0.0
				gstreamer-base-1.0 >= 1.0.0
				gstreamer-controller-1.0 >= 1.0.0
        		   ],
			   [
				AC_SUBST(GStreamer_CFLAGS)
				AC_SUBST(GStreamer_LIBS)
        		   ],
			   [
				AC_MSG_ERROR([Could not find GStreamer 1.0 libraries])
			   ])
	 AC_CHECK_LIB([gstapp-1.0], [gst_app_src_push_buffer],
			[LIBGSTAPP=-lgstapp-1.0],
			AC_MSG_ERROR([Could not find libgstapp]))
	 AC_SUBST([LIBGSTAPP])
	],
	[
	 AC_DEFINE([DISABLE_RTP_SERVER], [1],
		   [RTP Server disabled during config])
	])
AM_CONDITIONAL([RTPSRV], [test "x$with_rtp_server" != xno])

AS_IF([test "x$with_gtk" != xno],
	[PKG_CHECK_MODULES([GTK],
			   [
				gtk+-3.0 >= 3.16.0
			   ],
			   [
				AC_SUBST(GTK_CFLAGS)
				AC_SUBST(GTK_LIBS)
			   ],
			   [
				AC_MSG_ERROR([Could not find GTK+ >= 3.16])
			   ])
	 PKG_CHECK_MODULES([Epoxy],
			   [
				epoxy >= 1.3.0
			   ],
			   [
				AC_SUBST(Epoxy_CFLAGS)
				AC_SUBST(Epoxy_LIBS)
			   ],
			   [
				AC_MSG_ERROR([Could not find libepoxy >= 1.3.0])
			   ])
	],
	[
	 AC_DEFINE([DISABLE_GUI], [1],
		   [GUI disabled during config])
	])
AM_CONDITIONAL([GUI], [test "x$with_gtk" != xno])


# Check for headers
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdint.h stdlib.h string.h])
AC_CHECK_HEADER([math.h], [],
		AC_MSG_ERROR([Could not find math.h]))
AC_CHECK_HEADER([soxr-lsr.h], [],
		AC_MSG_ERROR([Could not find soxr-lsr.h]))
AC_CHECK_HEADER([jack/transport.h], [],
		AC_MSG_ERROR([Could not find jack/transport.h]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([ftruncate memset munmap pow strtol strnlen])

# Output files
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

