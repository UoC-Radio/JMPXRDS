bin_PROGRAMS = jmpxrds rds_tool fmmod_tool

jmpxrds_SOURCES = filters.c oscilator.c resampler.c rds_encoder.c rds_config.c fmmod.c main.c
jmpxrds_LDADD = $(LIBM) $(LIBRT) $(LIBSAMPLERATE) $(LIBJACK)

rds_tool_SOURCES = rds_config.c rds_tool.c
rds_tool_LDADD = $(LIBRT)

fmmod_tool_SOURCES = fmmod_tool.c
fmmod_tool_LDADD = $(LIBRT)

# Also clean up after autoconf
distclean-local:
	-rm -rf autom4te.cache
	-rm -rf build-aux
	-rm aclocal.m4
	-rm configure
	-rm *.in

# Do nothing
test:

