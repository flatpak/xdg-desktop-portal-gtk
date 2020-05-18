#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "background.h"
#include "fdonotification.h"
#include "request.h"
#include "utils.h"

static OrgGnomeShellIntrospect *shell;

typedef enum { BACKGROUND, RUNNING, ACTIVE } AppState;

static GVariant *app_state;

static char *
get_actual_app_id (const char *app_id)
{
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new (app_id);
  char *app = NULL;

  if (info)
    app = g_desktop_app_info_get_string (info, "X-Flatpak");

  return app;
}

static GVariant *
get_app_state (void)
{
  g_autoptr(GVariant) apps = NULL;
  g_autoptr(GHashTable) app_states = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GError) error = NULL;
  GVariantBuilder builder;
  GHashTableIter iter;
  const char *key;
  gpointer value;

  if (!org_gnome_shell_introspect_call_get_running_applications_sync (shell, &apps, NULL, NULL))
    {
      return NULL;
    }

  if (apps)
    {
      g_autoptr(GVariantIter) iter = g_variant_iter_new (apps);
      const char *app_id = NULL;
      GVariant *dict;

      while (g_variant_iter_loop (iter, "{&s@a{sv}}", &app_id, &dict))
        {
          const char *sandboxed_app_id = NULL;
          const char **seats = NULL;
          char *app;
          AppState state = BACKGROUND;

          g_variant_lookup (dict, "sandboxed-app-id", "&s", &sandboxed_app_id);
          g_variant_lookup (dict, "active-on-seats", "^a&s", &seats);

          /* See https://gitlab.gnome.org/GNOME/gnome-shell/issues/1289 */
          if (sandboxed_app_id)
            app = g_strdup (sandboxed_app_id);
          else
            app = get_actual_app_id (app_id);
          if (app == NULL)
            continue;

          state = GPOINTER_TO_INT (g_hash_table_lookup (app_states, app));
          state = MAX (state, RUNNING);
          if (seats != NULL)
            state = MAX (state, ACTIVE);

          g_hash_table_insert (app_states, app, GINT_TO_POINTER (state));
        }
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_hash_table_iter_init (&iter, app_states);
  while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&value))
    g_variant_builder_add (&builder, "{sv}", key, g_variant_new_uint32 (GPOINTER_TO_UINT (value)));

  return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static gboolean
handle_get_app_state (XdpImplBackground *object,
                      GDBusMethodInvocation *invocation)
{
  g_debug ("background: handle GetAppState");

  if (app_state == NULL)
    app_state = get_app_state ();

  if (app_state == NULL)
    g_dbus_method_invocation_return_error (invocation,
                                           XDG_DESKTOP_PORTAL_ERROR,
                                           XDG_DESKTOP_PORTAL_ERROR_FAILED,
                                           "Could not get window list");
  else
    xdp_impl_background_complete_get_app_state (object, invocation, app_state);

  return TRUE;
}

typedef enum {
  FORBID = 0,
  ALLOW  = 1,
  IGNORE = 2
} NotifyResult;

typedef struct {
  XdpImplBackground *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  char *id;
  NotifyResult result;
  char *name;
  GtkWidget *dialog;
} BackgroundHandle;

static void
background_handle_free (gpointer data)
{
  BackgroundHandle *handle = data;

  g_object_unref (handle->request);
  g_free (handle->id);
  g_free (handle->name);

  g_free (handle);
}

static void
background_handle_close (BackgroundHandle *handle)
{
  GDBusConnection *connection;

  connection = g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (handle->impl));
  fdo_remove_notification (connection, handle->request->app_id, handle->id);

  if (handle->dialog)
    {
      gtk_widget_destroy (handle->dialog);
      handle->dialog = NULL;
    }

  background_handle_free (handle);
}

static void
send_response (BackgroundHandle *handle)
{
  GVariantBuilder opt_builder;
  int response = 0;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}", "result", g_variant_new_uint32 (handle->result));

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_background_complete_notify_background (handle->impl,
                                                  handle->invocation,
                                                  response,
                                                  g_variant_builder_end (&opt_builder));

  background_handle_close (handle);
}

static void
response_received (GtkDialog *dialog,
                   int response,
                   gpointer data)
{
  BackgroundHandle *handle = data;

  switch (response)
    {
    case ALLOW:
      g_debug ("Allow app %s to run in background", handle->request->app_id);
      handle->result = ALLOW;
      break;

    case FORBID:
      g_debug ("Forbid app %s to run in background", handle->request->app_id);
      handle->result = FORBID;
      break;
    }

  send_response (handle);
}

static void
show_permission_dialog (BackgroundHandle *handle)
{
  GtkWidget *dialog;
  GtkWidget *button;

  dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, 
                                   _("“%s” is running in the background"), handle->name);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("This might be for a legitimate reason, but the application "
                                              "has not provided one."
                                              "\n\nNote that forcing an application to quit might cause data loss."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Force quit"), FORBID);
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Allow"), ALLOW);

  button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), FORBID);
  gtk_style_context_add_class (gtk_widget_get_style_context (button), "destructive-action");

  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);

  g_signal_connect (dialog, "response", G_CALLBACK (response_received), handle);

  handle->dialog = dialog;

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
activate_action (GDBusConnection *connection,
                 const char *app_id,
                 const char *id,
                 const char *name,
                 GVariant *parameter,
                 gpointer data)
{
  BackgroundHandle *handle = data;

  if (g_str_equal (name, "show"))
    {
      g_debug ("Show background permissions for %s", handle->request->app_id);
      show_permission_dialog (handle);
    }
  else
    {
      g_debug ("Unexpected action for app %s", handle->request->app_id);
      handle->result = IGNORE;
      send_response (handle);
    }
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              BackgroundHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  xdp_impl_background_complete_notify_background (handle->impl,
                                                  handle->invocation,
                                                  2,
                                                  g_variant_builder_end (&opt_builder));

  if (handle->request->exported)
    request_unexport (handle->request);

  background_handle_close (handle);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static int count;

static gboolean
handle_notify_background (XdpImplBackground *object,
                          GDBusMethodInvocation *invocation,
                          const char *arg_handle,
                          const char *arg_app_id,
                          const char *arg_name)
{
  g_autofree char *body = NULL;
  GVariantBuilder builder;
  GVariantBuilder bbuilder;
  GVariantBuilder button;
  GDBusConnection *connection;
  const char *sender;
  g_autoptr (Request) request = NULL;
  BackgroundHandle *handle;

  g_debug ("background: handle NotifyBackground %s %s", arg_app_id, arg_name);

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "title", g_variant_new_string (_("Background activity")));

  body = g_strdup_printf (_("“%s” is running in the background"), arg_name);
  g_variant_builder_add (&builder, "{sv}", "body", g_variant_new_string (body));
  g_variant_builder_add (&builder, "{sv}", "default-action", g_variant_new_string ("show"));

  g_variant_builder_init (&bbuilder, G_VARIANT_TYPE ("aa{sv}"));
  g_variant_builder_init (&button, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&button, "{sv}", "label", g_variant_new_string (_("Find out more")));
  g_variant_builder_add (&button, "{sv}", "action", g_variant_new_string ("show"));

  g_variant_builder_add (&bbuilder, "@a{sv}", g_variant_builder_end (&button));

  g_variant_builder_add (&builder, "{sv}", "buttons", g_variant_builder_end (&bbuilder));

  handle = g_new0 (BackgroundHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->name = g_strdup (arg_name);
  handle->id = g_strdup_printf ("notify_background_%d", count++);

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  connection = g_dbus_method_invocation_get_connection (invocation);

  fdo_add_notification (connection, "", handle->id, g_variant_builder_end (&builder), activate_action, handle);

  request_export (request, connection);

  return TRUE;
}

static gboolean
needs_quoting (const char *arg)
{
  while (*arg != 0)
    {
      char c = *arg;
      if (!g_ascii_isalnum (c) &&
          !(c == '-' || c == '/' || c == '~' ||
            c == ':' || c == '.' || c == '_' ||
            c == '=' || c == '@'))
        return TRUE;
      arg++;
    }
  return FALSE;
}

char *
flatpak_quote_argv (const char *argv[],
                    gssize      len)
{
  GString *res = g_string_new ("");
  int i;

  if (len == -1)
    len = g_strv_length ((char **) argv);

  for (i = 0; i < len; i++)
    {
      if (i != 0)
        g_string_append_c (res, ' ');

      if (needs_quoting (argv[i]))
        {
          g_autofree char *quoted = g_shell_quote (argv[i]);
          g_string_append (res, quoted);
        }
      else
        g_string_append (res, argv[i]);
    }

  return g_string_free (res, FALSE);
}

typedef enum {
  AUTOSTART_FLAGS_NONE        = 0,
  AUTOSTART_FLAGS_ACTIVATABLE = 1 << 0,
} AutostartFlags;

static gboolean
handle_enable_autostart (XdpImplBackground *object,
                         GDBusMethodInvocation *invocation,
                         const char *arg_app_id,
                         gboolean arg_enable,
                         const char * const *arg_commandline,
                         guint arg_flags)
{
  gboolean result = FALSE;
  g_autofree char *dir = NULL;
  g_autofree char *file = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *commandline = NULL;
  g_autoptr(GKeyFile) keyfile = NULL;

  g_debug ("background: handle EnableAutostart");

  file = g_strconcat (arg_app_id, ".desktop", NULL);
  dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  path = g_build_filename (dir, file, NULL);

  if (!arg_enable)
    {
      unlink (path);
      g_debug ("Removed %s", path);
      goto out;
    }

  if (g_mkdir_with_parents (dir, 0755) != 0)
    {
      g_warning ("Failed to create dirs %s", dir);
      goto out;
    }

  commandline = flatpak_quote_argv ((const char **)arg_commandline, -1);

  keyfile = g_key_file_new ();

  g_key_file_set_string (keyfile,
                         G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_TYPE,
                         "Application");
  g_key_file_set_string (keyfile,
                         G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_NAME,
                         arg_app_id); // FIXME
  g_key_file_set_string (keyfile,
                         G_KEY_FILE_DESKTOP_GROUP,
                         G_KEY_FILE_DESKTOP_KEY_EXEC,
                         commandline);
  if (arg_flags & AUTOSTART_FLAGS_ACTIVATABLE)
    g_key_file_set_boolean (keyfile,
                            G_KEY_FILE_DESKTOP_GROUP,
                            G_KEY_FILE_DESKTOP_KEY_DBUS_ACTIVATABLE,
                            TRUE);
  g_key_file_set_string (keyfile,
                         G_KEY_FILE_DESKTOP_GROUP,
                         "X-Flatpak",
                         arg_app_id);

  if (!g_key_file_save_to_file (keyfile, path, &error))
    {
      g_warning ("Failed to save %s: %s", path, error->message);
      goto out;
    }

  g_debug ("Wrote autostart file %s", path);

  result = TRUE;

out:
  xdp_impl_background_complete_enable_autostart (object, invocation, result);

  return TRUE;
}

static gboolean
compare_app_states (GVariant *a, GVariant *b)
{
  GVariantIter *iter;
  const char *app_id;
  GVariant *value;
  gboolean changed;

  changed = FALSE;

  iter = g_variant_iter_new (a);
  while (!changed && g_variant_iter_next (iter, "{&sv}", &app_id, &value))
    {
      guint v1, v2;

      g_variant_get (value, "u", &v1);

      if (!g_variant_lookup (b, app_id, "u", &v2))
        v2 = BACKGROUND;

      if ((v1 == BACKGROUND && v2 != BACKGROUND) ||
          (v1 != BACKGROUND && v2 == BACKGROUND))
        {
          g_debug ("App %s changed state: %u != %u", app_id, v1, v2);
          changed = TRUE;
        }
    }
  g_variant_iter_free (iter);

  return changed;
}

static void
running_applications_changed (OrgGnomeShellIntrospect *object,
                              GDBusInterfaceSkeleton *helper)
{
  GVariant *new_app_state;
  gboolean changed = FALSE;

  g_debug ("Received running-applications-changed from gnome-shell");

  new_app_state = get_app_state ();

  if (app_state == NULL || new_app_state == NULL)
    changed = TRUE;
  else if (compare_app_states (app_state, new_app_state) ||
           compare_app_states (new_app_state, app_state))
    changed = TRUE;

  if (app_state)
    g_variant_unref (app_state);
  app_state = new_app_state;

  if (changed)
    {
      g_debug ("Emitting RunningApplicationsChanged");
      xdp_impl_background_emit_running_applications_changed (XDP_IMPL_BACKGROUND (helper));
    }
}

gboolean
background_init (GDBusConnection *bus,
                 GError **error)
{
  GDBusInterfaceSkeleton *helper;

  shell = org_gnome_shell_introspect_proxy_new_sync (bus,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     "org.gnome.Shell",
                                                     "/org/gnome/Shell/Introspect",
                                                     NULL,
                                                     NULL);
  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_background_skeleton_new ());

  g_signal_connect (shell, "running-applications-changed", G_CALLBACK (running_applications_changed), helper);

  g_signal_connect (helper, "handle-get-app-state", G_CALLBACK (handle_get_app_state), NULL);
  g_signal_connect (helper, "handle-notify-background", G_CALLBACK (handle_notify_background), NULL);
  g_signal_connect (helper, "handle-enable-autostart", G_CALLBACK (handle_enable_autostart), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
