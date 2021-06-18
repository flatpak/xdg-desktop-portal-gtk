/*
 * Copyright © 2018 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#define _GNU_SOURCE 1

#include "config.h"

#include <string.h>

#include <gio/gio.h>

#include "xdg-desktop-portal-dbus.h"

#include "lockdown.h"
#include "utils.h"

static GSettings *lockdown;
static GSettings *location;
static GSettings *privacy;

gboolean
lockdown_init (GDBusConnection *bus,
               GError **error)
{
  GDBusInterfaceSkeleton *helper;
  GSettingsSchemaSource *source;
  GSettingsSchema *schema;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_lockdown_skeleton_new ());

  lockdown = g_settings_new ("org.gnome.desktop.lockdown");
  g_settings_bind (lockdown, "disable-printing", helper, "disable-printing", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (lockdown, "disable-save-to-disk", helper, "disable-save-to-disk", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (lockdown, "disable-application-handlers", helper, "disable-application-handlers", G_SETTINGS_BIND_DEFAULT);

  location = g_settings_new ("org.gnome.system.location");
  g_settings_bind (location, "enabled", helper, "disable-location", G_SETTINGS_BIND_INVERT_BOOLEAN);

  source = g_settings_schema_source_get_default ();
  schema = g_settings_schema_source_lookup (source, "org.gnome.desktop.privacy", TRUE);

  privacy = g_settings_new ("org.gnome.desktop.privacy");
  if (g_settings_schema_has_key (schema, "disable-camera"))
    g_settings_bind (privacy, "disable-camera", helper, "disable-camera", G_SETTINGS_BIND_DEFAULT);
  if (g_settings_schema_has_key (schema, "disable-microphone"))
    g_settings_bind (privacy, "disable-microphone", helper, "disable-microphone", G_SETTINGS_BIND_DEFAULT);
  if (g_settings_schema_has_key (schema, "disable-sound-output"))
    g_settings_bind (privacy, "disable-sound-output", helper, "disable-sound-output", G_SETTINGS_BIND_DEFAULT);

  g_settings_schema_unref (schema);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}

