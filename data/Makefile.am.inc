portaldir = $(datadir)/xdg-desktop-portal/portals
dist_portal_DATA = data/gtk.portal

%.service: data/%.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbus_servicedir = $(DBUS_SERVICE_DIR)
dbus_service_in_files = data/org.freedesktop.impl.portal.desktop.gtk.service.in
dbus_service_DATA = org.freedesktop.impl.portal.desktop.gtk.service

CLEANFILES += $(dbus_service_DATA)

EXTRA_DIST += \
	$(dbus_service_in_files)                \
        data/org.gtk.Notifications.xml          \
        data/org.gnome.SessionManager.xml       \
        data/org.gnome.Shell.Screenshot.xml     \
        data/org.freedesktop.ScreenSaver.xml    \
        data/org.freedesktop.Accounts.xml       \
        data/org.freedesktop.Accounts.User.xml  \
        $(NULL)
