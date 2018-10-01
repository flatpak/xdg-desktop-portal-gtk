/*
 * Copyright © 2016 Red Hat, Inc
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

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include <glib/gi18n.h>
#include <locale.h>

#include "xdg-desktop-portal-dbus.h"

#include "request.h"
#include "filechooser.h"
#include "appchooser.h"
#include "print.h"
#include "screenshot.h"
#include "notification.h"
#include "inhibit.h"
#include "access.h"
#include "account.h"
#include "email.h"
#include "screencast.h"
#include "remotedesktop.h"


static GMainLoop *loop = NULL;
static GHashTable *outstanding_handles = NULL;

static gboolean opt_verbose;
static gboolean opt_replace;

static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace, "Replace a running instance", NULL },
  { NULL }
};

static void
message_handler (const gchar *log_domain,
                 GLogLevelFlags log_level,
                 const gchar *message,
                 gpointer user_data)
{
  /* Make this look like normal console output */
  if (log_level & G_LOG_LEVEL_DEBUG)
    g_printerr ("XDP: %s\n", message);
  else
    g_printerr ("%s: %s\n", g_get_prgname (), message);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  GError *error = NULL;

  if (!file_chooser_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!app_chooser_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!print_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!screenshot_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!notification_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!inhibit_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!access_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!account_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!email_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!screen_cast_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }

  if (!remote_desktop_init (connection, &error))
    {
      g_warning ("error: %s\n", error->message);
      g_clear_error (&error);
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("org.freedesktop.impl.portal.desktop.gtk acquired");
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  g_autoptr(GError) error = NULL;
  GDBusConnection  *session_bus;
  GOptionContext *context;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Avoid pointless and confusing recursion */
  g_unsetenv ("GTK_USE_PORTAL");

  gtk_init (&argc, &argv);

  context = g_option_context_new ("- portal backends");
  g_option_context_set_summary (context,
      "A backend implementation for xdg-desktop-portal.");
  g_option_context_set_description (context,
      "xdg-desktop-portal-gtk provides D-Bus interfaces that\n"
      "are used by xdg-desktop-portal to implement portals\n"
      "\n"
      "Documentation for the available D-Bus interfaces can be found at\n"
      "https://flatpak.github.io/xdg-desktop-portal/portal-docs.html\n"
      "\n"
      "Please report issues at https://github.com/flatpak/xdg-desktop-portal-gtk/issues");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("option parsing failed: %s\n", error->message);
      return 1;
    }

  if (opt_verbose)
    g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, message_handler, NULL);

  g_set_prgname ("xdg-desktop-portal-gtk");

  loop = g_main_loop_new (NULL, FALSE);

  outstanding_handles = g_hash_table_new (g_str_hash, g_str_equal);

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (session_bus == NULL)
    {
      g_printerr ("No session bus: %s\n", error->message);
      return 2;
    }

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.impl.portal.desktop.gtk",
                             G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  g_main_loop_run (loop);

  g_bus_unown_name (owner_id);

  return 0;
}
