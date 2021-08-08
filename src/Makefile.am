lib_LTLIBRARIES = libdres.la
bin_PROGRAMS    = dresc

# Enable this to get a verbose bison output file (parser.output).
# AM_YFLAGS = -v

# DRES library
libdres_la_SOURCES = parser.y lexer.l \
                     action.c builtin.c target.c \
                     factvar.c dresvar.c variables.c \
                     prereq.c graph.c dres.c ast.c \
                     vm-stack.c vm-instr.c vm-global.c vm-local.c \
                     vm-method.c vm-debug.c vm-log.c vm.c \
                     compiler.c

libdres_la_CFLAGS  = @GLIB_CFLAGS@ @CCOPT_VISIBILITY_HIDDEN@
libdres_la_LIBADD  = @GLIB_LIBS@ @LIBTRACE_LIBS@ -lm
libdres_la_LDFLAGS = -version-info @LIBDRES_VERSION_INFO@

# DRES binary generator
dresc_SOURCES = dresc.c
dresc_CFLAGS  = @LIBOHMFACT_CFLAGS@ @GLIB_CFLAGS@
dresc_LDADD   = libdres.la @LIBOHMFACT_LIBS@ @GLIB_LIBS@ -lm


# various test programs
noinst_PROGRAMS = parser-test

parser_test_SOURCES = parser-test.c
parser_test_CFLAGS  = @LIBOHMFACT_CFLAGS@ @GLIB_CFLAGS@
parser_test_LDADD   = libdres.la @LIBOHMFACT_LIBS@ @GLIB_LIBS@ -lm

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~

