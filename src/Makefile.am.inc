dbus_built_sources = src/xdg-desktop-portal-dbus.c src/xdg-desktop-portal-dbus.h
BUILT_SOURCES += $(dbus_built_sources)
CLEANFILES += $(dbus_built_sources)

$(dbus_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			\
	--interface-prefix org.freedesktop.impl.portal.	\
	--c-namespace XdpImpl				\
	--generate-c-code src/xdg-desktop-portal-dbus	\
	--annotate "org.freedesktop.impl.portal.Print.Print()" "org.gtk.GDBus.C.UnixFD" "true" \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Request.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.FileChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.AppChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Print.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Screenshot.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Notification.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Inhibit.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Access.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Account.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Email.xml \
	$(NULL)

shell_built_sources = src/shell-dbus.c src/shell-dbus.h
BUILT_SOURCES += $(shell_built_sources)
CLEANFILES += $(shell_built_sources)

$(shell_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			                \
	--generate-c-code src/shell-dbus	                        \
	$(top_srcdir)/data/org.gnome.Shell.Screenshot.xml	        \
	$(top_srcdir)/data/org.gtk.Notifications.xml			\
	$(top_srcdir)/data/org.gnome.SessionManager.xml		        \
	$(top_srcdir)/data/org.freedesktop.ScreenSaver.xml		\
	$(top_srcdir)/data/org.freedesktop.Accounts.xml		        \
	$(top_srcdir)/data/org.freedesktop.Accounts.User.xml            \
	$(NULL)

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/src --generate-dependencies $(top_srcdir)/src/xdg-desktop-portal-gtk.gresource.xml)

src/resources.c: src/xdg-desktop-portal-gtk.gresource.xml $(resource_files)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir)/src --c-name _xdg_desktop --generate-source

EXTRA_DIST += \
	src/xdg-desktop-portal-gtk.gresource.xml	\
        src/appchooserdialog.ui                         \
        src/appchooserdialog.css                        \
        src/appchooserrow.ui                            \
        src/screenshotdialog.ui                         \
        src/accountdialog.ui                            \
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
	src/print.h				\
	src/print.c				\
	src/access.h				\
	src/access.c				\
	src/account.h				\
	src/account.c				\
	src/accountdialog.h			\
	src/accountdialog.c			\
	src/email.h				\
	src/email.c				\
        src/gtkbackports.h                      \
        src/gtkbackports.c                      \
	src/externalwindow.h			\
	src/externalwindow.c			\
	$(NULL)

nodist_xdg_desktop_portal_gtk_SOURCES = \
	src/resources.c				\
	$(dbus_built_sources)			\
	$(shell_built_sources)			\
	$(NULL)

if HAVE_GTK_X11
xdg_desktop_portal_gtk_SOURCES += \
	src/externalwindow-x11.h		\
	src/externalwindow-x11.c		\
	$(NULL)
endif

if HAVE_GTK_WAYLAND
xdg_desktop_portal_gtk_SOURCES += \
	src/externalwindow-wayland.h		\
	src/externalwindow-wayland.c		\
	$(NULL)
endif

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS) $(GTK_X11_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS) $(GTK_X11_CFLAGS)
xdg_desktop_portal_gtk_CPPFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\"        \
	-DLOCALEDIR=\"$(localedir)\"                    \
	-I$(top_srcdir)/src				\
	-I$(top_builddir)/src				\
	$(NULL)

noinst_PROGRAMS = \
        testappchooser

testappchooser_LDADD = $(GTK_LIBS) $(GTK_X11_LIBS)
testappchooser_CFLAGS = $(GTK_CFLAGS) $(GTK_X11_CFLAGS)
testappchooser_CPPFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\"        \
	-DLOCALEDIR=\"$(localedir)\"                    \
	-I$(top_srcdir)/src				\
	-I$(top_builddir)/src				\
	$(NULL)

testappchooser_SOURCES = \
        src/testappchooser.c                    \
	src/appchooserrow.h			\
	src/appchooserrow.c			\
	src/appchooserdialog.h		        \
	src/appchooserdialog.c		        \
	src/resources.c                         \
        $(NULL)
