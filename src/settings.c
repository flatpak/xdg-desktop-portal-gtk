/*
 * Copyright © 2018 Igalia S.L.
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
 *       Patrick Griffis <pgriffis@igalia.com>
 */

#include "config.h"

#include <time.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "settings.h"
#include "utils.h"
#include "shellintrospect.h"

#include "xdg-desktop-portal-dbus.h"
#include "fc-monitor.h"

static GHashTable *settings;
static FcMonitor *fontconfig_monitor;
static int fontconfig_serial;
static gboolean enable_animations;

static void sync_animations_enabled (XdpImplSettings *impl, ShellIntrospect *shell_introspect);

typedef struct {
  GSettingsSchema *schema;
  GSettings *settings;
} SettingsBundle;

static SettingsBundle *
settings_bundle_new (GSettingsSchema *schema,
                     GSettings       *settings)
{
  SettingsBundle *bundle = g_new (SettingsBundle, 1);
  bundle->schema = schema;
  bundle->settings = settings;
  return bundle;
}

static void
settings_bundle_free (SettingsBundle *bundle)
{
  g_object_unref (bundle->schema);
  g_object_unref (bundle->settings);
  g_free (bundle);
}

static gboolean
namespace_matches (const char         *namespace,
                   const char * const *patterns)
{
  size_t i;

  for (i = 0; patterns[i]; ++i)
    {
      size_t pattern_len;
      const char *pattern = patterns[i];

      if (pattern[0] == '\0')
        return TRUE;
      if (strcmp (namespace, pattern) == 0)
        return TRUE;

      pattern_len = strlen (pattern);
      if (pattern[pattern_len - 1] == '*' && strncmp (namespace, pattern, pattern_len - 1) == 0)
        return TRUE;
    }

  if (i == 0) /* Empty array */
    return TRUE;

  return FALSE;
}

static GVariant *
get_color_scheme (void)
{
  SettingsBundle *bundle = g_hash_table_lookup (settings, "org.gnome.desktop.interface");
  int color_scheme;

  if (!g_settings_schema_has_key (bundle->schema, "color-scheme"))
    return g_variant_new_uint32 (0); /* No preference */

  color_scheme = g_settings_get_enum (bundle->settings, "color-scheme");

  return g_variant_new_uint32 (color_scheme);
}

static gboolean
settings_handle_read_all (XdpImplSettings       *object,
                          GDBusMethodInvocation *invocation,
                          const char * const    *arg_namespaces,
                          gpointer               data)
{
  g_autoptr(GVariantBuilder) builder = g_variant_builder_new (G_VARIANT_TYPE ("(a{sa{sv}})"));
  GHashTableIter iter;
  char *key;
  SettingsBundle *value;

  g_variant_builder_open (builder, G_VARIANT_TYPE ("a{sa{sv}}"));

  g_hash_table_iter_init (&iter, settings);
  while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&value))
    {
      g_auto (GStrv) keys = NULL;
      GVariantDict dict;
      gsize i;

      if (!namespace_matches (key, arg_namespaces))
        continue;

      keys = g_settings_schema_list_keys (value->schema);
      g_variant_dict_init (&dict, NULL);
      for (i = 0; keys[i]; ++i)
        {
          if (strcmp (key, "org.gnome.desktop.interface") == 0 &&
              strcmp (keys[i], "enable-animations") == 0)
            g_variant_dict_insert_value (&dict, keys[i], g_variant_new_boolean (enable_animations));
          else
            g_variant_dict_insert_value (&dict, keys[i], g_settings_get_value (value->settings, keys[i]));
        }

      g_variant_builder_add (builder, "{s@a{sv}}", key, g_variant_dict_end (&dict));
    }

  if (namespace_matches ("org.gnome.fontconfig", arg_namespaces))
    {
      GVariantDict dict;

      g_variant_dict_init (&dict, NULL);
      g_variant_dict_insert_value (&dict, "serial", g_variant_new_int32 (fontconfig_serial));

      g_variant_builder_add (builder, "{s@a{sv}}", "org.gnome.fontconfig", g_variant_dict_end (&dict));
    }

  if (namespace_matches ("org.freedesktop.appearance", arg_namespaces))
    {
      GVariantDict dict;

      g_variant_dict_init (&dict, NULL);
      g_variant_dict_insert_value (&dict, "color-scheme", get_color_scheme ());

      g_variant_builder_add (builder, "{s@a{sv}}", "org.freedesktop.appearance", g_variant_dict_end (&dict));
    }

  g_variant_builder_close (builder);

  g_dbus_method_invocation_return_value (invocation, g_variant_builder_end (builder));

  return TRUE;
}

static gboolean
settings_handle_read (XdpImplSettings       *object,
                      GDBusMethodInvocation *invocation,
                      const char            *arg_namespace,
                      const char            *arg_key,
                      gpointer               data)
{
  g_debug ("Read %s %s", arg_namespace, arg_key);

  if (strcmp (arg_namespace, "org.gnome.fontconfig") == 0)
    {
      if (strcmp (arg_key, "serial") == 0)
        {
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)", g_variant_new_int32 (fontconfig_serial)));
          return TRUE;
        }
    }
  else if (strcmp (arg_namespace, "org.freedesktop.appearance") == 0 &&
           strcmp (arg_key, "color-scheme") == 0)
    {
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(v)", get_color_scheme ()));
      return TRUE;
    }
  else if (strcmp (arg_namespace, "org.gnome.desktop.interface") == 0 &&
           strcmp (arg_key, "enable-animations") == 0)
    {
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("(v)", g_variant_new_boolean (enable_animations)));
      return TRUE;
    }
  else if (g_hash_table_contains (settings, arg_namespace))
    {
      SettingsBundle *bundle = g_hash_table_lookup (settings, arg_namespace);
      if (g_settings_schema_has_key (bundle->schema, arg_key))
        {
          g_autoptr (GVariant) variant = NULL;
          variant = g_settings_get_value (bundle->settings, arg_key);
          g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", variant));
          return TRUE;
        }
    }

  g_debug ("Attempted to read unknown namespace/key pair: %s %s", arg_namespace, arg_key);
  g_dbus_method_invocation_return_error_literal (invocation, XDG_DESKTOP_PORTAL_ERROR,
                                                 XDG_DESKTOP_PORTAL_ERROR_NOT_FOUND,
                                                 _("Requested setting not found"));

  return TRUE;
}

typedef struct {
  XdpImplSettings *self;
  const char *namespace;
} ChangedSignalUserData;

static ChangedSignalUserData *
changed_signal_user_data_new (XdpImplSettings *settings,
                              const char      *namespace)
{
  ChangedSignalUserData *data = g_new (ChangedSignalUserData, 1);
  data->self = settings;
  data->namespace = namespace;
  return data;
}

static void
changed_signal_user_data_destroy (gpointer  data,
                                  GClosure *closure)
{
  g_free (data);
}

static void
on_settings_changed (GSettings             *settings,
                     const char            *key,
                     ChangedSignalUserData *user_data)
{
  g_autoptr (GVariant) new_value = g_settings_get_value (settings, key);

  g_debug ("Emitting changed for %s %s", user_data->namespace, key);
  if (strcmp (user_data->namespace, "org.gnome.desktop.interface") == 0 &&
      strcmp (key, "enable-animations") == 0)
    sync_animations_enabled (user_data->self, shell_introspect_get ());
  else
    xdp_impl_settings_emit_setting_changed (user_data->self,
                                            user_data->namespace, key,
                                            g_variant_new ("v", new_value));

  if (strcmp (user_data->namespace, "org.gnome.desktop.interface") == 0 &&
      strcmp (key, "color-scheme") == 0)
    xdp_impl_settings_emit_setting_changed (user_data->self,
                                            "org.freedesktop.appearance", key,
                                            g_variant_new ("v", get_color_scheme ()));
}

static void
init_settings_table (XdpImplSettings *settings,
                     GHashTable      *table)
{
  static const char * const schemas[] = {
    "org.gnome.desktop.interface",
    "org.gnome.settings-daemon.peripherals.mouse",
    "org.gnome.desktop.sound",
    "org.gnome.desktop.privacy",
    "org.gnome.desktop.wm.preferences",
    "org.gnome.settings-daemon.plugins.xsettings",
    "org.gnome.desktop.a11y",
    "org.gnome.desktop.a11y.interface",
    "org.gnome.desktop.input-sources",
  };
  size_t i;
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();

  for (i = 0; i < G_N_ELEMENTS(schemas); ++i)
    {
      GSettings *setting;
      GSettingsSchema *schema;
      SettingsBundle *bundle;
      const char *schema_name = schemas[i];

      schema = g_settings_schema_source_lookup (source, schema_name, TRUE);
      if (!schema)
        {
          g_debug ("%s schema not found", schema_name);
          continue;
        }

      setting = g_settings_new (schema_name);
      bundle = settings_bundle_new (schema, setting);
      g_signal_connect_data (setting, "changed", G_CALLBACK(on_settings_changed),
                             changed_signal_user_data_new (settings, schema_name),
                             changed_signal_user_data_destroy, 0);
      g_hash_table_insert (table, (char*)schema_name, bundle);
    }
}

static void
fontconfig_changed (FcMonitor       *monitor,
                    XdpImplSettings *impl)
{
  const char *namespace = "org.gnome.fontconfig";
  const char *key = "serial";

  g_debug ("Emitting changed for %s %s", namespace, key);

  fontconfig_serial++;

  xdp_impl_settings_emit_setting_changed (impl,
                                          namespace, key,
                                          g_variant_new ("v", g_variant_new_int32 (fontconfig_serial)));
}

static void
set_enable_animations (XdpImplSettings *impl,
                       gboolean         new_enable_animations)
{
  const char *namespace = "org.gnome.desktop.interface";
  const char *key = "enable-animations";
  GVariant *enable_animations_variant;

  if (enable_animations == new_enable_animations)
    return;

  enable_animations = new_enable_animations;
  enable_animations_variant =
    g_variant_new ("v", g_variant_new_boolean (enable_animations));
  xdp_impl_settings_emit_setting_changed (impl,
                                          namespace,
                                          key,
                                          enable_animations_variant);
}

static void
sync_animations_enabled (XdpImplSettings *impl,
                         ShellIntrospect *shell_introspect)
{
  gboolean new_enable_animations;

  if (!shell_introspect_are_animations_enabled (shell_introspect,
                                                &new_enable_animations))
    {
      SettingsBundle *bundle = g_hash_table_lookup (settings, "org.gnome.desktop.interface");
      new_enable_animations = g_settings_get_boolean (bundle->settings, "enable-animations");
    }

  set_enable_animations (impl, new_enable_animations);
}

static void
animations_enabled_changed (ShellIntrospect *shell_introspect,
                            XdpImplSettings *impl)
{
  sync_animations_enabled (impl, shell_introspect);
}

gboolean
settings_init (GDBusConnection  *bus,
               GError          **error)
{
  GDBusInterfaceSkeleton *helper;
  ShellIntrospect *shell_introspect;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_settings_skeleton_new ());

  g_signal_connect (helper, "handle-read", G_CALLBACK (settings_handle_read), NULL);
  g_signal_connect (helper, "handle-read-all", G_CALLBACK (settings_handle_read_all), NULL);

  settings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)settings_bundle_free);

  init_settings_table (XDP_IMPL_SETTINGS (helper), settings);

  fontconfig_monitor = fc_monitor_new ();
  g_signal_connect (fontconfig_monitor, "updated", G_CALLBACK (fontconfig_changed), helper);
  fc_monitor_start (fontconfig_monitor);

  shell_introspect = shell_introspect_get ();
  g_signal_connect (shell_introspect, "animations-enabled-changed",
                    G_CALLBACK (animations_enabled_changed),
                    helper);
  sync_animations_enabled (XDP_IMPL_SETTINGS (helper),
                           shell_introspect);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;

}
