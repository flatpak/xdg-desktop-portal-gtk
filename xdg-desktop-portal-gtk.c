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

#include "xdg-desktop-portal-dbus.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "xdg-desktop-portal-gtk.h"
#include "filechooser.h"
#include "appchooser.h"


static GMainLoop *loop = NULL;
static GHashTable *outstanding_handles = NULL;

static gboolean opt_verbose;
static gboolean opt_replace;

static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "replace", 'r', 0, G_OPTION_ARG_NONE, &opt_replace, "Replace", NULL },
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

DialogHandle *
dialog_handle_find (const char *arg_sender,
                    const char *arg_app_id,
                    const char *arg_handle,
                    GType skel_type)
{
  DialogHandle *handle;

  handle = g_hash_table_lookup (outstanding_handles, arg_handle);

  if (handle != NULL &&
      (/* App is unconfined */
       strcmp (arg_app_id, "") == 0 ||
       /* or same app */
       strcmp (handle->app_id, arg_app_id) == 0) &&
      g_type_check_instance_is_a ((GTypeInstance *)handle->skeleton, skel_type))
    return handle;

  return NULL;
}

void
dialog_handle_register (DialogHandle *handle)
{
  guint32 r;

  r = g_random_int ();
  do
    {
      g_free (handle->id);
      handle->id = g_strdup_printf ("/org/freedesktop/portal/desktop/%u", r);
    }
  while (g_hash_table_lookup (outstanding_handles, handle->id) != NULL);

  g_hash_table_insert (outstanding_handles, handle->id, handle);
}

void
dialog_handle_unregister (DialogHandle *handle)
{
  g_hash_table_remove (outstanding_handles, handle->id);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  g_autoptr(GError) error = NULL;
  GDBusConnection  *session_bus;
  GOptionContext *context;

  setlocale (LC_ALL, "");

  /* Avoid even loading gvfs to avoid accidental confusion */
  g_setenv ("GIO_USE_VFS", "local", TRUE);

  gtk_init (&argc, &argv);

  context = g_option_context_new ("- file chooser portal");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("option parsing failed: %s\n", error->message);
      return 1;
    }

  if (opt_verbose)
    g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, message_handler, NULL);

  g_set_prgname (argv[0]);

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
