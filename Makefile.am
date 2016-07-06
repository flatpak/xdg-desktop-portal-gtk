NULL =

SUBDIRS = po

BUILT_SOURCES =
EXTRA_DIST =

include src/Makefile.am.inc
include data/Makefile.am.inc

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/xdg-desktop-portal-gtk.gresource.xml)

resources.h: xdg-desktop-portal-gtk.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_desktop --generate-header

resources.c: xdg-desktop-portal-gtk.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(srcdir) --c-name _xdg_desktop --generate-source

EXTRA_DIST = \
	xdg-desktop-portal-gtk.gresource.xml	\
	org.gtk.Notifications.xml		\
	org.gnome.SessionManager.xml		\
	org.freedesktop.ScreenSaver.xml		\
	$(NULL)

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS)
xdg_desktop_portal_gtk_CPPFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" 	\
	-DLOCALEDIR=\"$(localedir)\" 			\
	$(NULL)
