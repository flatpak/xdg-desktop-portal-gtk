AC_PREREQ([2.63])

AC_INIT([xdg-desktop-portal-gtk],[1.14.1])

AC_PROG_CC
AM_PROG_CC_C_O
AC_DISABLE_STATIC

LT_PREREQ([2.2.6])
LT_INIT([disable-static])

AC_CONFIG_SRCDIR([src/xdg-desktop-portal-gtk.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 no-define no-dist-gzip dist-xz tar-ustar foreign subdir-objects])
AC_PROG_SED

# Enable silent rules is available
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[[\ \	]]-Wall[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi

# i18n stuff
GETTEXT_PACKAGE=xdg-desktop-portal-gtk
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
                   [The prefix for our gettext translation domains.])

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.18.3)

PKG_PROG_PKG_CONFIG([0.24])

AC_ARG_WITH(dbus_service_dir,
        AS_HELP_STRING([--with-dbus-service-dir=PATH],[choose directory for dbus service files, [default=PREFIX/share/dbus-1/services]]),
        with_dbus_service_dir="$withval", with_dbus_service_dir=$datadir/dbus-1/services)
DBUS_SERVICE_DIR=$with_dbus_service_dir
AC_SUBST(DBUS_SERVICE_DIR)

AC_ARG_WITH([systemduserunitdir],
            [AS_HELP_STRING([--with-systemduserunitdir=DIR],
                            [Directory for systemd user service files (default=PREFIX/lib/systemd/user)])],
    [],
    dnl This is deliberately not ${libdir}: systemd units always go in
    dnl .../lib, never .../lib64 or .../lib/x86_64-linux-gnu
    [with_systemduserunitdir='${prefix}/lib/systemd/user'])
AC_SUBST([systemduserunitdir], [$with_systemduserunitdir])

AC_SUBST([DESKTOP_PORTAL_INTERFACES_DIR], [`$PKG_CONFIG --variable=interfaces_dir xdg-desktop-portal`])
AC_SUBST([DBUS_INTERFACES_DIR], [`$PKG_CONFIG --variable=interfaces_dir dbus-1`])
AC_SUBST([GDBUS_CODEGEN], [`$PKG_CONFIG --variable gdbus_codegen gio-2.0`])
AC_SUBST([GLIB_COMPILE_RESOURCES], [`$PKG_CONFIG --variable glib_compile_resources gio-2.0`])

AC_ARG_ENABLE([wallpaper],
              [AS_HELP_STRING([--enable-wallpaper],[Build wallpaper portal. Needs gnome-desktop])])
AS_IF([test "x$enable_wallpaper" = "xyes"], [
    PKG_CHECK_MODULES(WALLPAPER, [gnome-desktop-3.0])
    AC_DEFINE([BUILD_WALLPAPER], [1], [Define to enable wallpaper portal])
])
AM_CONDITIONAL([BUILD_WALLPAPER],[test "$enable_wallpaper" = "yes"])

AC_ARG_ENABLE([screenshot],
              [AS_HELP_STRING([--enable-screenshot],[Build screenshot portal. Needs gnome-shell])])
AS_IF([test "x$enable_screenshot" = "xyes"], [
    AC_DEFINE([BUILD_SCREENSHOT], [1], [Define to enable screenshot portal])
])
AM_CONDITIONAL([BUILD_SCREENSHOT],[test "$enable_screenshot" = "yes"])

AC_ARG_ENABLE([screencast],
              [AS_HELP_STRING([--enable-screencast],[Build screencast and remote desktop portals. Needs mutter])])
AS_IF([test "x$enable_screencast" = "xyes"], [
    AC_DEFINE([BUILD_SCREENCAST], [1], [Define to enable screencast portal])
])
AM_CONDITIONAL([BUILD_SCREENCAST],[test "$enable_screencast" = "yes"])

AC_ARG_ENABLE([background],
              [AS_HELP_STRING([--enable-background],[Build background portal. Needs gnome-shell])])
AS_IF([test "x$enable_background" = "xyes"], [
    AC_DEFINE([BUILD_BACKGROUND], [1], [Define to enable background portal])
])
AM_CONDITIONAL([BUILD_BACKGROUND],[test "$enable_background" = "yes"])

AC_ARG_ENABLE([settings],
              [AS_HELP_STRING([--disable-settings],[Disable the settings portal, which needs dconf])])
AS_IF([test "x$enable_settings" != "xno"], [
    PKG_CHECK_MODULES(SETTINGS, [fontconfig gsettings-desktop-schemas])
    AC_DEFINE([BUILD_SETTINGS], [1], [Define to enable settings portal])
])
AM_CONDITIONAL([BUILD_SETTINGS],[test "$enable_settings" != "no"])

AC_ARG_ENABLE([appchooser],
              [AS_HELP_STRING([--disable-appchooser],[Disable the appchooser portal.])])
AS_IF([test "x$enable_appchooser" != "xno"], [
    AC_DEFINE([BUILD_APPCHOOSER], [1], [Define to enable appchooser portal])
])
AM_CONDITIONAL([BUILD_APPCHOOSER],[test "$enable_appchooser" != "no"])

AC_ARG_ENABLE(lockdown,
              [AS_HELP_STRING([--enable-lockdown],[Build lockdown portal.])],
              enable_lockdown=$enableval, enable_lockdown=yes)
if test x$enable_lockdown = xyes; then
    AC_DEFINE([BUILD_LOCKDOWN], [1], [Define to enable lockdown portal])
fi
AM_CONDITIONAL([BUILD_LOCKDOWN],[test "$enable_lockdown" = "yes"])

PKG_CHECK_MODULES(GTK, [xdg-desktop-portal >= 1.14.0 glib-2.0 >= 2.44 gio-unix-2.0 gtk+-3.0 >= 3.14 gtk+-unix-print-3.0])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GTK_X11, gtk+-x11-3.0,
		  have_gtk_x11=yes, have_gtk_x11=no)
if test "$have_gtk_x11" = "yes"; then
	AC_DEFINE(HAVE_GTK_X11, 1, [define if we we have gtk+-x11])
	AC_SUBST(GTK_X11_CFLAGS)
	AC_SUBST(GTK_X11_LIBS)
fi
AM_CONDITIONAL([HAVE_GTK_X11], [test "$have_gtk_x11" = "yes"])

PKG_CHECK_MODULES(GTK_WAYLAND, gtk+-wayland-3.0 >= 3.21.5,
		  have_gtk_wayland=yes, have_gtk_wayland=no)
if test "$have_gtk_wayland" = "yes"; then
	AC_DEFINE(HAVE_GTK_WAYLAND, 1, [define if we we have gtk+-wayland])
	AC_SUBST(GTK_WAYLAND_CFLAGS)
	AC_SUBST(GTK_WAYLAND_LIBS)
fi
AM_CONDITIONAL([HAVE_GTK_WAYLAND], [test "$have_gtk_wayland" = "yes"])

AC_CONFIG_FILES([
Makefile
po/Makefile.in
])
AC_OUTPUT
