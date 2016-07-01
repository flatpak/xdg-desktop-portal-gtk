SUBDIRS = po

NULL =

libexec_PROGRAMS = \
	xdg-desktop-portal-gtk \
	$(NULL)

portaldir = $(datadir)/xdg-desktop-portal/portals
portal_DATA = gtk.portal

dbus_built_sources = xdg-desktop-portal-dbus.c xdg-desktop-portal-dbus.h
BUILT_SOURCES = $(dbus_built_sources)

$(dbus_built_sources): Makefile.am
	$(AM_V_GEN) $(GDBUS_CODEGEN)			\
	--interface-prefix org.freedesktop.impl.portal.	\
	--c-namespace XdpImpl				\
	--generate-c-code xdg-desktop-portal-dbus	\
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Request.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.FileChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.AppChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Print.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Screenshot.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Notification.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Inhibit.xml \
	$(NULL)

shell_built_sources = shell-dbus.c shell-dbus.h
BUILT_SOURCES += $(shell_built_sources)

$(shell_built_sources): Makefile.am
	$(AM_V_GEN) $(GDBUS_CODEGEN)			\
	--generate-c-code shell-dbus	\
	$(DBUS_INTERFACES_DIR)/org.gnome.Shell.Screenshot.xml	\
	$(top_srcdir)/org.gtk.Notifications.xml				\
	$(top_srcdir)/org.gnome.SessionManager.xml			\
	$(NULL)

%.service: %.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

servicedir = $(DBUS_SERVICE_DIR)
service_in_files = org.freedesktop.impl.portal.desktop.gtk.service.in
service_DATA = org.freedesktop.impl.portal.desktop.gtk.service

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
	$(NULL)


DISTCLEANFILES = \
	$(NULL)

xdg_desktop_portal_gtk_SOURCES = \
	xdg-desktop-portal-gtk.c	\
	utils.h				\
	request.h			\
	request.c			\
	filechooser.h			\
	filechooser.c			\
	appchooser.h			\
	appchooser.c			\
	notification.h			\
	notification.c			\
	inhibit.h			\
	inhibit.c			\
	appchooserrow.h			\
	appchooserrow.c			\
	appchooserdialog.h		\
	appchooserdialog.c		\
	screenshot.h			\
	screenshot.c			\
	screenshotdialog.h		\
	screenshotdialog.c		\
	resources.c			\
	print.h				\
	print.c				\
	$(dbus_built_sources)		\
	$(shell_built_sources)		\
	$(NULL)

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS)
xdg_desktop_portal_gtk_CPPFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(NULL)

if FALSE
bin_PROGRAMS = test

test_SOURCES = \
	appchooserdialog.h	\
	appchooserdialog.c	\
	appchooserrow.h		\
	appchooserrow.c		\
	resources.c		\
	test.c

test_LDADD = $(BASE_LIBS) $(GTK_LIBS)
test_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS)
endif
