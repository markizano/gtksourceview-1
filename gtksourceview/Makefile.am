INCLUDES = \
	-DDATADIR=\""$(datadir)"\" \
	-DG_LOG_DOMAIN=\"GtkSourceView\" \
	-DG_DISABLE_DEPRECATED \
	-DGTK_DISABLE_DEPRECATED \
	-DGDK_DISABLE_DEPRECATED \
	-I$(top_srcdir) -I$(srcdir) \
	$(WARN_CFLAGS) \
	$(DEP_CFLAGS)

BUILT_SOURCES = \
	gtksourceview-marshal.c \
	gtksourceview-marshal.h

# libs
lib_LTLIBRARIES = libgtksourceview.la

libgtksourceview_la_SOURCES = \
	gtksourceview-marshal.h \
	gtksourceview-marshal.c \
	gtksourcebuffer.c \
	gtksourcebuffer.h \
	gtksourcetag.c \
	gtksourcetag.h \
	gtksourceview.c \
	gtksourceview.h \
	gtkundomanager.h \
	gtkundomanager.c \
	gtktextregion.h \
	gtktextregion.c \
	gtksourceregex.h \
	gtksourceregex.c \
	gtksourcelanguage.c \
	gtksourcelanguage.h \
	gtksourcelanguagesmanager.h \
	gtksourcetagstyle.h \
	gtksourcetagstyle.c \
	gtksourceview-i18n.h \
	gtksourceview-i18n.c

libgtksourceview_la_LIBADD = $(DEP_LIBS)

libgtksourceview_includedir = $(includedir)/gtksourceview

libgtksourceview_include_HEADERS = \
	gtksourcebuffer.h \
	gtksourcetag.h \
	gtksourceview.h \
	gtksourceregex.h \
	gtksourcelanguage.h \
	gtksourcelanguagesmanager.h \
	gtksourcetagstyle.h \
	gtktextregion.h

gtksourceview-marshal.h: gtksourceview-marshal.list
	@GLIB_GENMARSHAL@ \
		--prefix=gtksourceview_marshal $(srcdir)/gtksourceview-marshal.list --header > xgen-gmc \
	&& cp xgen-gmc $(@F) \
	&& rm -f xgen-gmc

gtksourceview-marshal.c: gtksourceview-marshal.h
	echo "#include \"gtksourceview-marshal.h\"" > xgen-gmc \
	&& @GLIB_GENMARSHAL@ \
		--prefix=gtksourceview_marshal $(srcdir)/gtksourceview-marshal.list --body >> xgen-gmc \
	&& cp xgen-gmc $(@F) \
	&& rm -f xgen-gmc

# testing executables
noinst_PROGRAMS = test-widget testregion

test_widget_SOURCES = test-widget.c

test_widget_LDADD = \
	libgtksourceview.la \
	$(DEP_LIBS)

testregion_SOURCES = testregion.c

testregion_LDADD = \
	libgtksourceview.la \
	$(DEP_LIBS)

EXTRA_DIST = gtksourceview-marshal.list
