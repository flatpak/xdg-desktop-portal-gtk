portaldir = $(datadir)/xdg-desktop-portal/portals
dist_portal_DATA = data/gtk.portal

%.service: data/%.service.in config.log
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbus_servicedir = $(DBUS_SERVICE_DIR)
dbus_service_in_files = data/org.freedesktop.impl.portal.desktop.gtk.service.in
dbus_service_DATA = org.freedesktop.impl.portal.desktop.gtk.service

CLEANFILES += $(dbus_service_DATA)

systemduserunit_in_files = data/xdg-desktop-portal-gtk.service.in
systemduserunit_DATA = xdg-desktop-portal-gtk.service

CLEANFILES += $(systemduserunit_DATA)

%.desktop.in: data/%.desktop.in.in
	$(AM_V_GEN) sed -e "s|@libexecdir[@]|$(libexecdir)|" $< > $@

%.desktop: %.desktop.in
	$(AM_V_GEN) msgfmt --desktop -d po --template $< -o $@

desktopdir = $(datadir)/applications
desktop_in_in_files = data/xdg-desktop-portal-gtk.desktop.in.in
dist_desktop_DATA = xdg-desktop-portal-gtk.desktop

CLEANFILES += $(dist_desktop_DATA)

EXTRA_DIST += \
	$(dbus_service_in_files)                \
        $(systemduserunit_in_files)             \
        $(desktop_in_in_files)                  \
        data/org.gtk.Notifications.xml          \
        data/org.gnome.SessionManager.xml       \
        data/org.gnome.Shell.Screenshot.xml     \
        data/org.freedesktop.ScreenSaver.xml    \
        data/org.freedesktop.Accounts.xml       \
        data/org.freedesktop.Accounts.User.xml  \
        $(NULL)
