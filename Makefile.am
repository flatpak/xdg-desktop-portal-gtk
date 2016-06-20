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
	--c-namespace Xdp			\
	--generate-c-code xdg-desktop-portal-dbus		\
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.FileChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.AppChooser.xml \
	$(NULL)

%.service: %.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

servicedir = $(DBUS_SERVICE_DIR)
service_in_files = org.freedesktop.impl.portal.desktop.gtk.service.in
service_DATA = org.freedesktop.impl.portal.desktop.gtk.service

xdg_desktop_portal_gtk_SOURCES = \
	xdg-desktop-portal-gtk.c	\
	xdg-desktop-portal-gtk.h	\
	filechooser.h			\
	filechooser.c			\
	appchooser.h			\
	appchooser.c			\
	$(dbus_built_sources)		\
	$(NULL)

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS)
