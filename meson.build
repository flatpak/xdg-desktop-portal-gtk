project(
  'xdg-desktop-portal-gtk',
  'c',
  version: '1.15.2',
  meson_version: '>= 0.61.2',
  license: 'LGPL-2.1-or-later',
  default_options: ['warning_level=2'],
)

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
libexecdir = prefix / get_option('libexecdir')
sysconfdir = prefix / get_option('sysconfdir')
localedir = prefix / get_option('localedir')
dbus_service_dir = get_option('dbus-service-dir')
if dbus_service_dir == ''
  dbus_service_dir = prefix /  datadir / 'dbus-1' / 'services'
endif

systemd_userunit_dir = get_option('systemd-user-unit-dir')
if systemd_userunit_dir == ''
  # This is deliberately not ${libdir}: systemd units always go in
  # .../lib, never .../lib64 or .../lib/x86_64-linux-gnu
  systemd_userunit_dir = prefix / 'lib' / 'systemd' / 'user'
endif

dataroot_dir = get_option('datarootdir')
if dataroot_dir == ''
  dataroot_dir = datadir
endif

i18n = import('i18n')
gnome = import('gnome')
pkgconfig = import('pkgconfig')

root_inc = include_directories('.')

config_h = configuration_data()

config_h.set_quoted('G_LOG_DOMAIN', 'xdg-desktop-portal-gtk')
config_h.set_quoted('DATADIR', datadir)
config_h.set_quoted('LIBEXECDIR', libexecdir)
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('SYSCONFDIR', sysconfdir)
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('PACKAGE_NAME', meson.project_name())
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), meson.project_version()))

xdp_dep = dependency('xdg-desktop-portal')
desktop_portal_interfaces_dir = xdp_dep.get_variable(pkgconfig: 'interfaces_dir')

portal_deps = [
  dependency('xdg-desktop-portal', version: '>=1.14.0'),
  dependency('glib-2.0', version: '>=2.44'),
  dependency('gio-unix-2.0'),
  dependency('gtk+-3.0', version: '>=3.14'),
  dependency('gtk+-unix-print-3.0'),
]

gtkx11_dep = dependency('gtk+-x11-3.0', required: false)
config_h.set('HAVE_GTK_X11', gtkx11_dep.found())

gtkwayland_dep = dependency('gtk+-wayland-3.0', version: '>=3.21.5', required: false)
config_h.set('HAVE_GTK_WAYLAND', gtkwayland_dep.found())

configure_file(output: 'config.h', configuration: config_h)

subdir('data')
subdir('src')
subdir('po')

summary({
    'DBus service dir': dbus_service_dir,
    'Portal interfaces dir': desktop_portal_interfaces_dir,
    'systemd user unit dir': systemd_userunit_dir,
  },
  section: 'Directories',
)

summary({
    'X11': gtkx11_dep.found(),
    'Wayland': gtkwayland_dep.found(),
  },
  bool_yn: true,
  section: 'Dependencies',
)

summary({
    'Wallpaper': get_option('wallpaper'),
    'Settings': get_option('settings'),
    'AppChooser': get_option('appchooser'),
    'Lockdown': get_option('lockdown'),
  },
  bool_yn: true,
  section: 'Portals',
)
