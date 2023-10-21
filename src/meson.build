portal_sources = files(
  'utils.c',
  'request.c',
  'session.c',
  'filechooser.c',
  'notification.c',
  'fdonotification.c',
  'inhibit.c',
  'print.c',
  'access.c',
  'account.c',
  'accountdialog.c',
  'email.c',
  'gtkbackports.c',
  'externalwindow.c',
  'dynamic-launcher.c',
)

portal_c_args = [
  '-DLOCALEDIR="@0@"'.format(localedir),
]

if get_option('wallpaper').allowed()
  portal_deps += dependency('gnome-desktop-3.0')
  portal_sources += files(
    'wallpaper.c',
    'wallpaperpreview.c',
    'wallpaperdialog.c',
  )
  portal_c_args += '-DBUILD_WALLPAPER'
endif

if get_option('settings').allowed()
  portal_deps += [
    dependency('gsettings-desktop-schemas'),
    dependency('fontconfig'),
  ]
  portal_sources += files(
    'fc-monitor.c',
    'settings.c',
  )
  portal_c_args += '-DBUILD_SETTINGS'
endif

if get_option('appchooser').allowed()
  portal_sources += files(
    'appchooser.c',
    'appchooserrow.c',
    'appchooserdialog.c',
  )
  portal_c_args += '-DBUILD_APPCHOOSER'
endif

if get_option('lockdown').allowed()
  portal_sources += files('lockdown.c')
  portal_c_args += '-DBUILD_LOCKDOWN'
endif

if gtkx11_dep.found()
  portal_sources += files('externalwindow-x11.c')
endif

if gtkwayland_dep.found()
  portal_sources += files('externalwindow-wayland.c')
endif

portal_resources = gnome.compile_resources(
  'xdg-desktop-portal-gtk-resources',
  'xdg-desktop-portal-gtk.gresource.xml',
  c_name: '_xdg_desktop',
  source_dir: '.',
)

portal_built_sources = [
  portal_resources,
]

portal_built_sources += gnome.gdbus_codegen('xdg-desktop-portal-dbus',
  sources: portal_dbus_interfaces,
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
  annotations: [
    [ 'org.freedesktop.impl.portal.Print.Print()', 'org.gtk.GDBus.C.UnixFD', 'true' ],
  ],
)

portal_built_sources += gnome.gdbus_codegen('shell-dbus',
  sources: [
    gtk_dbus_interfaces,
    fdo_dbus_interfaces,
    gnome_dbus_interfaces,
  ],
)

executable('xdg-desktop-portal-gtk',
  sources: [
    'xdg-desktop-portal-gtk.c',
    portal_sources,
    portal_built_sources,
  ],
  dependencies: [
    portal_deps,
    gtkx11_dep,
    gtkwayland_dep,
  ],
  c_args: portal_c_args,
  include_directories: [root_inc],
  install: true,
  install_dir: libexecdir,
)

executable('testappchooser',
  sources: [
    'testappchooser.c',
    'appchooserrow.c',
    'appchooserdialog.c',
    portal_resources,
  ],
  dependencies: [
    portal_deps,
    gtkx11_dep,
  ],
  c_args: [
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLOCALEDIR="@0@"'.format(localedir),
  ],
  include_directories: [root_inc],
)
