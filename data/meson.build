portal_dbus_interfaces = [
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Request.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Session.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.FileChooser.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Print.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Notification.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Inhibit.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Access.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Account.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Email.xml',
  desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.DynamicLauncher.xml',
]

fdo_dbus_interfaces = files(
  'org.freedesktop.ScreenSaver.xml',
  'org.freedesktop.Accounts.xml',
  'org.freedesktop.Accounts.User.xml',
)

gtk_dbus_interfaces = files('org.gtk.Notifications.xml')

gnome_dbus_interfaces = files(
  'org.gnome.SessionManager.xml',
  'org.gnome.ScreenSaver.xml',
)

gtk_portal = configuration_data()

if get_option('wallpaper').allowed()
  portal_dbus_interfaces += desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Wallpaper.xml'
  gtk_portal.set('wallpaper_iface', 'org.freedesktop.impl.portal.Wallpaper;')
else
  gtk_portal.set('wallpaper_iface', '')
endif

if get_option('settings').allowed()
  portal_dbus_interfaces += desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Settings.xml'
  gtk_portal.set('settings_iface', 'org.freedesktop.impl.portal.Settings;')
else
  gtk_portal.set('settings_iface', '')
endif

if get_option('appchooser').allowed()
  portal_dbus_interfaces += desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.AppChooser.xml'
  gtk_portal.set('appchooser_iface', 'org.freedesktop.impl.portal.AppChooser;')
else
  gtk_portal.set('appchooser_iface', '')
endif

if get_option('lockdown').allowed()
  portal_dbus_interfaces += desktop_portal_interfaces_dir / 'org.freedesktop.impl.portal.Lockdown.xml'
  gtk_portal.set('lockdown_iface', 'org.freedesktop.impl.portal.Lockdown;')
else
  gtk_portal.set('lockdown_iface', '')
endif

configure_file(
  input: 'gtk.portal.in',
  output: 'gtk.portal',
  configuration: gtk_portal,
  install: true,
  install_dir: datadir / 'xdg-desktop-portal/portals',
)

service_conf = configuration_data()
service_conf.set('libexecdir', libexecdir)
configure_file(
  input: 'org.freedesktop.impl.portal.desktop.gtk.service.in',
  output: 'org.freedesktop.impl.portal.desktop.gtk.service',
  configuration: service_conf,
  install: true,
  install_dir: dbus_service_dir,
)
configure_file(
  input: 'xdg-desktop-portal-gtk.service.in',
  output: 'xdg-desktop-portal-gtk.service',
  configuration: service_conf,
  install: true,
  install_dir: systemd_userunit_dir,
)

desktop_conf = configuration_data()
desktop_conf.set('libexecdir', libexecdir)
i18n.merge_file(
  type: 'desktop',
  input: configure_file(
    input: 'xdg-desktop-portal-gtk.desktop.in.in',
    output: 'xdg-desktop-portal-gtk.desktop.in',
    configuration: desktop_conf,
  ),
  output: 'xdg-desktop-portal-gtk.desktop',
  po_dir: '../po',
  install: true,
  install_dir: datadir / 'applications',
)
  
