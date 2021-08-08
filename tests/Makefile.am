noinst_PROGRAMS = dres-test fs-test

dres_test_SOURCES = dres-test.c
dres_test_CFLAGS  = @LIBOHMFACT_CFLAGS@      \
                    @GLIB_CFLAGS@ @LIBTRACE_CFLAGS@

dres_test_LDADD   = ../src/libdres.la     \
                    @LIBOHMFACT_LIBS@        \
                    @GLIB_LIBS@ @LIBTRACE_LIBS@

fs_test_SOURCES = fs-test.c
fs_test_CFLAGS  = @LIBOHMFACT_CFLAGS@ @GLIB_CFLAGS@
fs_test_LDADD   = @LIBOHMFACT_LIBS@ @GLIB_LIBS@

INCLUDES = -I$(top_builddir)/include
