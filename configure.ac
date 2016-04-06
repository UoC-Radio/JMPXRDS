# Prelude
AC_INIT([jmpxrds], [0.5], [mickflemm+jmpxrds@gmail.com])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign -Wall -Werror dist-bzip2])


# Define macros
AC_ARG_WITH([wave-table],
	AS_HELP_STRING([--with-wave-table],
			[Use a wave table for the oscilators instead of sin()/cos()]),
	AC_DEFINE([USE_WAVE_TABLE], [1],
			[Define this to use wave table for the oscilators]),
	[])
AC_DEFINE([USE_CUBIC_INTERPOLATION], [1],
	  [Define this to use cubic interpolation on the wave table (default)])
#AC_DEFINE([USE_LINEAR_INTERPOLATION], [0],
#	  [Define this to use linear interpolation on the wave table (not good)])

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
AC_CHECK_LIB([jack], [jack_client_open], [LIBJACK=-ljack],
	     AC_MSG_ERROR([Could not find jack libraries]))
AC_SUBST([LIBJACK])


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
