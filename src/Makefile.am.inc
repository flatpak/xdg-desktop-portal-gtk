dbus_built_sources = src/xdg-desktop-portal-dbus.c src/xdg-desktop-portal-dbus.h
BUILT_SOURCES += $(dbus_built_sources)
CLEANFILES += $(dbus_built_sources)

if BUILD_WALLPAPER
        WALLPAPER_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Wallpaper.xml
endif

if BUILD_SCREENSHOT
        SCREENSHOT_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Screenshot.xml
        SHELL_SCREENSHOT_IFACE=$(top_srcdir)/data/org.gnome.Shell.Screenshot.xml
endif

if BUILD_SCREENCAST
        SCREENCAST_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.ScreenCast.xml
        REMOTE_DESKTOP_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.RemoteDesktop.xml
        MUTTER_SCREENCAST_IFACE=$(top_srcdir)/data/org.gnome.Mutter.ScreenCast.xml
        MUTTER_REMOTE_DESKTOP_IFACE=$(top_srcdir)/data/org.gnome.Mutter.RemoteDesktop.xml
        MUTTER_DISPLAYCONFIG_IFACE=$(top_srcdir)/data/org.gnome.Mutter.DisplayConfig.xml
endif

if BUILD_BACKGROUND
        BACKGROUND_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Background.xml
endif

if BUILD_SETTINGS
        SETTINGS_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Settings.xml
endif

if BUILD_BACKGROUND
        SHELL_INTROSPECT_IFACE=$(top_srcdir)/data/org.gnome.Shell.Introspect.xml
else
if BUILD_SETTINGS
        SHELL_INTROSPECT_IFACE=$(top_srcdir)/data/org.gnome.Shell.Introspect.xml
endif
endif

if BUILD_APPCHOOSER
        APPCHOOSER_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.AppChooser.xml
endif

if BUILD_LOCKDOWN
        LOCKDOWN_IFACE=$(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Lockdown.xml
endif

$(dbus_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			\
	--interface-prefix org.freedesktop.impl.portal.	\
	--c-namespace XdpImpl				\
	--generate-c-code src/xdg-desktop-portal-dbus	\
	--annotate "org.freedesktop.impl.portal.Print.Print()" "org.gtk.GDBus.C.UnixFD" "true" \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Request.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Session.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.FileChooser.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Print.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Notification.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Inhibit.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Access.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Account.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.Email.xml \
	 $(DESKTOP_PORTAL_INTERFACES_DIR)/org.freedesktop.impl.portal.DynamicLauncher.xml \
	 $(WALLPAPER_IFACE) \
         $(SCREENSHOT_IFACE) \
         $(SCREENCAST_IFACE) \
         $(REMOTE_DESKTOP_IFACE) \
         $(BACKGROUND_IFACE) \
         $(SETTINGS_IFACE) \
         $(APPCHOOSER_IFACE) \
         $(LOCKDOWN_IFACE) \
	$(NULL)

shell_built_sources = src/shell-dbus.c src/shell-dbus.h
BUILT_SOURCES += $(shell_built_sources)
CLEANFILES += $(shell_built_sources)

$(shell_built_sources): src/Makefile.am.inc
	$(AM_V_GEN) $(GDBUS_CODEGEN)			                \
	--generate-c-code src/shell-dbus	                        \
	$(top_srcdir)/data/org.gtk.Notifications.xml			\
	$(top_srcdir)/data/org.gnome.SessionManager.xml		        \
	$(top_srcdir)/data/org.gnome.ScreenSaver.xml			\
	$(top_srcdir)/data/org.freedesktop.ScreenSaver.xml		\
	$(top_srcdir)/data/org.freedesktop.Accounts.xml		        \
	$(top_srcdir)/data/org.freedesktop.Accounts.User.xml            \
        $(SHELL_SCREENSHOT_IFACE)                                       \
        $(MUTTER_SCREENCAST_IFACE)                                      \
        $(MUTTER_REMOTE_DESKTOP_IFACE)                                  \
        $(MUTTER_DISPLAYCONFIG_IFACE)                                   \
        $(SHELL_INTROSPECT_IFACE)                                       \
	$(NULL)

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/src --generate-dependencies $(top_srcdir)/src/xdg-desktop-portal-gtk.gresource.xml)

src/resources.c: src/xdg-desktop-portal-gtk.gresource.xml $(resource_files)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir)/src --c-name _xdg_desktop --generate-source
CLEANFILES += src/resources.c

EXTRA_DIST += \
	src/xdg-desktop-portal-gtk.gresource.xml	\
        src/appchooserdialog.ui                         \
        src/appchooserrow.ui                            \
        src/screenshotdialog.ui                         \
        src/screenshotdialog.css                        \
        src/accountdialog.ui                            \
        src/screencastwidget.ui                         \
        src/screencastdialog.ui                         \
        src/remotedesktopdialog.ui                      \
        src/wallpaperdialog.ui                          \
        src/wallpaperpreview.ui                         \
        src/wallpaperpreview.css                        \
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
	src/session.c   			\
	src/session.h	        		\
	src/filechooser.h			\
	src/filechooser.c			\
	src/notification.h			\
	src/notification.c			\
	src/fdonotification.h			\
	src/fdonotification.c			\
	src/inhibit.h			        \
	src/inhibit.c			        \
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
	src/dynamic-launcher.h			\
	src/dynamic-launcher.c			\
	$(NULL)

if BUILD_WALLPAPER
xdg_desktop_portal_gtk_SOURCES += \
	src/wallpaperdialog.c				\
	src/wallpaperdialog.h				\
	src/wallpaperpreview.c				\
	src/wallpaperpreview.h				\
	src/wallpaper.c				\
	src/wallpaper.h				\
	$(NULL)
endif

if BUILD_SCREENSHOT
xdg_desktop_portal_gtk_SOURCES += \
	src/screenshot.h			\
	src/screenshot.c			\
	src/screenshotdialog.h		        \
	src/screenshotdialog.c		        \
	$(NULL)
endif

if BUILD_SCREENCAST
xdg_desktop_portal_gtk_SOURCES += \
	src/screencast.c			\
	src/screencast.h			\
	src/screencastwidget.c			\
	src/screencastwidget.h			\
	src/screencastdialog.c			\
	src/screencastdialog.h			\
	src/gnomescreencast.c			\
	src/gnomescreencast.h			\
	src/remotedesktop.c			\
	src/remotedesktop.h			\
	src/remotedesktopdialog.c		\
	src/remotedesktopdialog.h		\
	src/displaystatetracker.c		\
	src/displaystatetracker.h		\
	$(NULL)
endif

if BUILD_BACKGROUND
xdg_desktop_portal_gtk_SOURCES += \
        src/background.c                        \
        src/background.h                        \
	$(NULL)
endif

if BUILD_SETTINGS
xdg_desktop_portal_gtk_SOURCES += \
        src/fc-monitor.c                        \
        src/fc-monitor.h                        \
        src/settings.c                          \
        src/settings.h                          \
	$(NULL)
endif

if BUILD_BACKGROUND
xdg_desktop_portal_gtk_SOURCES += \
	src/shellintrospect.c			\
	src/shellintrospect.h			\
	$(NULL)
else
if BUILD_SETTINGS
xdg_desktop_portal_gtk_SOURCES += \
	src/shellintrospect.c			\
	src/shellintrospect.h			\
	$(NULL)
endif
endif

if BUILD_APPCHOOSER
xdg_desktop_portal_gtk_SOURCES += \
	src/appchooser.h			\
	src/appchooser.c			\
	src/appchooserrow.h			\
	src/appchooserrow.c			\
	src/appchooserdialog.h		        \
	src/appchooserdialog.c		        \
	$(NULL)
endif

if BUILD_LOCKDOWN
xdg_desktop_portal_gtk_SOURCES += \
	src/lockdown.c				\
	src/lockdown.h				\
	$(NULL)
endif

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

xdg_desktop_portal_gtk_LDADD = $(BASE_LIBS) $(GTK_LIBS) $(GTK_X11_LIBS) $(WALLPAPER_LIBS) $(SETTINGS_LIBS)
xdg_desktop_portal_gtk_CFLAGS = $(BASE_CFLAGS) $(GTK_CFLAGS) $(GTK_X11_CFLAGS) $(WALLPAPER_CFLAGS) $(SETTINGS_CFLAGS)
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
        $(NULL)

nodist_testappchooser_SOURCES = \
	src/resources.c				\
	$(NULL)
