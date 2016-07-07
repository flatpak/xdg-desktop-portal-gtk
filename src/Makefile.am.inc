dbus_built_sources = src/xdg-desktop-portal-dbus.c src/xdg-desktop-portal-dbus.h
BUILT_SOURCES += $(dbus_built_sources)

$(dbus_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			\
	--interface-prefix org.freedesktop.impl.portal.	\
	--c-namespace XdpImpl				\
	--generate-c-code src/xdg-desktop-portal-dbus	\
	--annotate "org.freedesktop.impl.portal.Print.PrintFile()" "org.gtk.GDBus.C.UnixFD" "true" \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Request.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.FileChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.AppChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Print.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Screenshot.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Notification.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Inhibit.xml \
	$(NULL)

shell_built_sources = src/shell-dbus.c src/shell-dbus.h
BUILT_SOURCES += $(shell_built_sources)

$(shell_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			                \
	--generate-c-code src/shell-dbus	                        \
	$(top_srcdir)/data/org.gnome.Shell.Screenshot.xml	        \
	$(top_srcdir)/data/org.gtk.Notifications.xml			\
	$(top_srcdir)/data/org.gnome.SessionManager.xml		        \
	$(top_srcdir)/data/org.freedesktop.ScreenSaver.xml		\
	$(NULL)

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/src --generate-dependencies $(top_srcdir)/src/xdg-desktop-portal-gtk.gresource.xml)

src/resources.h: src/xdg-desktop-portal-gtk.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir)/src --c-name _xdg_desktop --generate-header

src/resources.c: src/xdg-desktop-portal-gtk.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir)/src --c-name _xdg_desktop --generate-source

EXTRA_DIST += \
	src/xdg-desktop-portal-gtk.gresource.xml	\
	$(NULL)

libexec_PROGRAMS = \
	xdg-desktop-portal-gtk \
	$(NULL)

xdg_desktop_portal_gtk_SOURCES = \
	src/xdg-desktop-portal-gtk.c	        \
	src/utils.h				\
	src/utils.c				\
	src/request.h			        \
	src/request.c			        \
	src/filechooser.h			\
	src/filechooser.c			\
	src/appchooser.h			\
	src/appchooser.c			\
	src/notification.h			\
	src/notification.c			\
	src/inhibit.h			        \
	src/inhibit.c			        \
	src/appchooserrow.h			\
	src/appchooserrow.c			\
	src/appchooserdialog.h		        \
	src/appchooserdialog.c		        \
	src/screenshot.h			\
	src/screenshot.c			\
	src/screenshotdialog.h		        \
	src/screenshotdialog.c		        \
	src/resources.c			        \
	src/print.h				\
	src/print.c				\
        src/gtkbackports.h                      \
        src/gtkbackports.c                      \
	$(dbus_built_sources)		        \
	$(shell_built_sources)		        \
	$(NULL)

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS)
xdg_desktop_portal_gtk_CPPFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\"        \
	-DLOCALEDIR=\"$(localedir)\"                    \
	$(NULL)
