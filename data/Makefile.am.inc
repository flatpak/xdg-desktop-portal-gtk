portaldir = $(datadir)/xdg-desktop-portal/portals
portal_DATA = data/gtk.portal

%.service: %.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

servicedir = $(DBUS_SERVICE_DIR)
service_in_files = data/org.freedesktop.impl.portal.desktop.gtk.service.in
service_DATA = data/org.freedesktop.impl.portal.desktop.gtk.service

EXTRA_DIST += \
        data/org.gtk.Notification.xml           \
        data/org.gnome.SessionManager.xml       \
        data/org.freedesktop.Screensaver.xml    \
        data/org.gnome.Shell.Screenshot.xml     \
        $(NULL)
