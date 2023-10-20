# xdg-desktop-portal-gtk

A backend implementation for [xdg-desktop-portal](http://github.com/flatpak/xdg-desktop-portal)
that is using GTK and various pieces of GNOME infrastructure, such as
org.gnome.desktop.* GSettings schemas and the org.gnome.SessionManager and
org.gnome.Screensaver D-Bus interfaces.

## Building xdg-desktop-portal-gtk

xdg-desktop-portal-gtk depends on xdg-desktop-portal and GTK.

## Versioning

The xdg-desktop-portal-gtk version has three components:

- major, incremented for backward compatibility breaking changes
- minor, incremented for changes in dependencies and exposed interfaces
- micro, incremented for bug fixes
