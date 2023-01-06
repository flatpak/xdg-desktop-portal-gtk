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

#include <string.h>

#include <gtk/gtk.h>
#include <gtk/gtkunixprint.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <gio/gdesktopappinfo.h>

#include "xdg-desktop-portal-dbus.h"

#include "print.h"
#include "request.h"
#include "utils.h"
#include "externalwindow.h"

#include "gtkbackports.h"

typedef struct {
  char *app_id;
  GtkPageSetup *page_setup;
  GtkPrintSettings *settings;
  GtkPrinter *printer;
  guint timeout_id;
  guint32 token;
  gboolean preview;
} PrintParams;

static GHashTable *print_params;

static void
print_params_free (gpointer data)
{
  PrintParams *params = data;

  g_hash_table_remove (print_params, GUINT_TO_POINTER (params->token));

  g_free (params->app_id);
  g_object_unref (params->page_setup);
  g_object_unref (params->settings);
  g_object_unref (params->printer);

  g_free (params);
}

static void
ensure_print_params (void)
{
  if (print_params)
    return;

  print_params = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static gboolean
print_params_timeout (gpointer data)
{
  print_params_free (data);
  g_debug ("Removing print params, now %d", g_hash_table_size (print_params));
  return G_SOURCE_REMOVE;
}

static PrintParams *
print_params_new (const char *app_id,
                  gboolean preview,
                  GtkPageSetup *page_setup,
                  GtkPrintSettings *settings,
                  GtkPrinter *printer)
{
  PrintParams *params;
  guint32 r;

  ensure_print_params ();

  params = g_new0 (PrintParams, 1);
  params->app_id = g_strdup (app_id);
  params->page_setup = g_object_ref (page_setup);
  params->settings = g_object_ref (settings);
  params->printer = g_object_ref (printer);
  params->preview = preview;

  do {
    r = g_random_int ();
  } while (r == 0 || g_hash_table_lookup (print_params, GUINT_TO_POINTER (r)) != NULL);

  params->token = r;
  g_hash_table_insert (print_params, GUINT_TO_POINTER (r), params);

  g_debug ("Remembering print params for %s, token %u, now %d",
           params->app_id, params->token,
           g_hash_table_size (print_params));

  params->timeout_id = g_timeout_add_seconds (300, print_params_timeout, params);

  return params;
}

static PrintParams *
get_print_params (const char *app_id,
                  guint32 token)
{
  PrintParams *params;

  params = (PrintParams *)g_hash_table_lookup (print_params, GUINT_TO_POINTER (token));
  if (params == NULL)
    return NULL;

  if (strcmp (params->app_id, app_id) != 0)
    return NULL;

  g_source_remove (params->timeout_id);
  g_hash_table_remove (print_params, GUINT_TO_POINTER (token));

  return params;
}

typedef struct {
  XdpImplPrint *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int fd;
  int response;

  PrintParams *params;

} PrintDialogHandle;

static void
print_dialog_handle_free (gpointer data)
{
  PrintDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  close (handle->fd);

  g_free (handle);
}

static void
print_dialog_handle_close (PrintDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  print_dialog_handle_free (handle);
}

static gboolean
can_preview (void)
{
  g_autofree char *path = NULL;

  path = g_find_program_in_path ("evince-previewer");

  if (path)
    return TRUE;

  g_warning ("evince-previewer not found, disabling print preview");

  return FALSE;
}

static gboolean
launch_preview (const char *filename,
                const char *title,
                GtkPrintSettings *settings,
                GtkPageSetup *page_setup,
                GError **error)
{
  g_autofree char *cmd = NULL;
  g_autoptr(GAppInfo) appinfo = NULL;
  g_autoptr(GAppLaunchContext) context = NULL;
  gboolean retval = FALSE;
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autofree char *data = NULL;
  gsize data_len;
  g_autofree char *settings_filename = NULL;
  int fd;

  fd = g_file_open_tmp ("settingsXXXXXX.ini", &settings_filename, error);
  if (fd == -1)
    goto out;

  keyfile = g_key_file_new ();

  if (settings)
    gtk_print_settings_to_key_file (settings, keyfile, NULL);

  if (page_setup)
    gtk_page_setup_to_key_file (page_setup, keyfile, NULL);

  g_key_file_set_string (keyfile, "Print Job", "title", title);

  data = g_key_file_to_data (keyfile, &data_len, NULL);
  if (data)
    g_file_set_contents (settings_filename, data, data_len, NULL);

  cmd = g_strdup_printf ("evince-previewer --unlink-tempfile --print-settings %s %s", settings_filename, filename);

  g_debug ("launching %s", cmd);

  appinfo = g_app_info_create_from_commandline (cmd,
                                                "Print Preview",
                                                G_APP_INFO_CREATE_NONE,
                                                error);
  if (!appinfo)
    goto out;

  context = (GAppLaunchContext *)gdk_display_get_app_launch_context (gdk_display_get_default ());
  retval = g_app_info_launch (appinfo, NULL, G_APP_LAUNCH_CONTEXT (context), error);

out:
  if (fd > 0)
    close (fd);

  return retval;
}

static gboolean
print_file (int fd,
            const char *app_id,
            gboolean preview,
            GtkPrinter *printer,
            GtkPrintSettings *settings,
            GtkPageSetup *page_setup,
            GError **error)
{
  g_autoptr (GtkPrintJob) job = NULL;
  g_autofree char *title = NULL;
  g_autofree char *filename = NULL;
  g_autoptr(GUnixInputStream) istream = NULL;
  g_autoptr(GUnixOutputStream) ostream = NULL;
  int fd2;
  g_autofree char *app_desktop_fullname = NULL;
  g_autoptr (GDesktopAppInfo) app_info = NULL;
  // ensures the app_name won't be NULL even if the app_id.desktop file doesn't exist
  g_autofree char *app_name = NULL;

  if (!g_str_has_suffix (app_id, ".desktop"))
    {
      app_desktop_fullname = g_strconcat (app_id, ".desktop", NULL);
    }
  else
    {
      app_desktop_fullname = g_strdup (app_id);
    }

  app_info = g_desktop_app_info_new (app_desktop_fullname);
  if (app_info)
    {
      app_name = g_desktop_app_info_get_locale_string (app_info, "Name");
    }

  if (app_name == NULL)
    app_name = g_strdup (app_id);

  title = g_strdup_printf ("Document from %s", app_name);

  if (!preview)
    job = gtk_print_job_new (title, printer, settings, page_setup);

#if GTK_CHECK_VERSION (3, 22, 0)
  if (job && !gtk_print_job_set_source_fd (job, fd, error))
    return FALSE;
#endif

  istream = (GUnixInputStream *)g_unix_input_stream_new (fd, FALSE);

  if ((fd2 = g_file_open_tmp (PACKAGE_NAME "XXXXXX", &filename, error)) == -1)
    return FALSE;

  ostream = (GUnixOutputStream *)g_unix_output_stream_new (fd2, TRUE);

  if (g_output_stream_splice (G_OUTPUT_STREAM (ostream),
                              G_INPUT_STREAM (istream),
                              G_OUTPUT_STREAM_SPLICE_NONE,
                              NULL,
                              error) == -1)
    return FALSE;
  if (job)
    {
      if (!gtk_print_job_set_source_file (job, filename, error))
        return FALSE;

      gtk_print_job_send (job, NULL, NULL, NULL);
    }
  else
    {
      if (!launch_preview (filename, title, settings, page_setup, error))
	{
	  unlink (filename);
	  return FALSE;
	}
    }

  /* The file will be removed when the GtkPrintJob closes it (once the job is
   * complete).
   */
  if (!preview)
    unlink (filename);

  return TRUE;
}

static void
handle_print_response (GtkDialog *dialog,
                       gint response,
                       gpointer data)
{
  PrintDialogHandle *handle = data;
  g_autoptr(GError) error = NULL;
  gboolean preview = FALSE;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      handle->response = 2;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      break;

    case GTK_RESPONSE_APPLY:
      preview = TRUE;
      /* fall thru */

    case GTK_RESPONSE_OK:
      {
        GtkPrinter *printer;
        GtkPrintSettings *settings;
        GtkPageSetup *page_setup;

        printer = gtk_print_unix_dialog_get_selected_printer (GTK_PRINT_UNIX_DIALOG (handle->dialog));
        settings = gtk_print_unix_dialog_get_settings (GTK_PRINT_UNIX_DIALOG (handle->dialog));
        page_setup = gtk_print_unix_dialog_get_page_setup (GTK_PRINT_UNIX_DIALOG (handle->dialog));

        if (!print_file (handle->fd,
                         handle->request->app_id,
                         preview,
                         printer,
                         settings,
                         page_setup,
                         &error))
          handle->response = 2;
        else
          handle->response = 0;

        g_object_unref (settings);
      }
      break;
    }

  if (handle->request->exported)
    request_unexport (handle->request);

  if (error)
    {
      g_dbus_method_invocation_take_error (handle->invocation, error);
    }
  else
    {
      GVariantBuilder opt_builder;

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      xdp_impl_print_complete_print (handle->impl,
                                     handle->invocation,
                                     NULL,
                                     handle->response,
                                     g_variant_builder_end (&opt_builder));
    }

  print_dialog_handle_close (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              PrintDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_print_complete_print (handle->impl,
                                 handle->invocation,
                                 NULL,
                                 2,
                                 g_variant_builder_end (&opt_builder));

  if (handle->request->exported)
    request_unexport (handle->request);

  print_dialog_handle_close (handle);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static gboolean
handle_print (XdpImplPrint *object,
              GDBusMethodInvocation *invocation,
              GUnixFDList *fd_list,
              const char *arg_handle,
              const char *arg_app_id,
              const char *arg_parent_window,
              const char *arg_title,
              GVariant *arg_fd_in,
              GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  GtkWidget *dialog;
  PrintDialogHandle *handle;
  guint32 token = 0;
  PrintParams *params;
  int idx, fd;
  gboolean modal;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  g_variant_get (arg_fd_in, "h", &idx);
  fd = g_unix_fd_list_get (fd_list, idx, NULL);

  g_variant_lookup (arg_options, "token", "u", &token);
  params = get_print_params (arg_app_id, token);
  if (params)
    {
      GVariantBuilder opt_builder;

      print_file (fd,
                  params->app_id,
		  params->preview,
                  params->printer,
                  params->settings,
                  params->page_setup,
                  NULL);

      print_params_free (params);

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      xdp_impl_print_complete_print (object,
                                     invocation,
                                     NULL,
                                     0,
                                     g_variant_builder_end (&opt_builder));
      return TRUE;
    }

  sender = g_dbus_method_invocation_get_sender (invocation);
  request = request_new (sender, arg_app_id, arg_handle);

  if (arg_parent_window)
    {
      external_parent = create_external_window_from_handle (arg_parent_window);
      if (!external_parent)
        g_warning ("Failed to associate portal window with parent window %s",
                   arg_parent_window);
    }

  if (external_parent)
    display = external_window_get_display (external_parent);
  else
    display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);

  fake_parent = g_object_new (GTK_TYPE_WINDOW,
                              "type", GTK_WINDOW_TOPLEVEL,
                              "screen", screen,
                              NULL);
  g_object_ref_sink (fake_parent);

  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;

  dialog = gtk_print_unix_dialog_new (arg_title, NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), modal);
  gtk_print_unix_dialog_set_manual_capabilities (GTK_PRINT_UNIX_DIALOG (dialog),
                                                 can_preview () ? GTK_PRINT_CAPABILITY_PREVIEW : 0 |
						 GTK_PRINT_CAPABILITY_PAGE_SET |
						 GTK_PRINT_CAPABILITY_COPIES |
						 GTK_PRINT_CAPABILITY_COLLATE |
						 GTK_PRINT_CAPABILITY_REVERSE |
						 GTK_PRINT_CAPABILITY_SCALE |
						 GTK_PRINT_CAPABILITY_NUMBER_UP
						 );
  handle = g_new0 (PrintDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;
  handle->fd = fd;

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "response", G_CALLBACK (handle_print_response), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  gtk_widget_show (dialog);

  return TRUE;
}

static void
send_prepare_print_response (PrintDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (handle->response == 0)
    {
      g_variant_builder_add (&opt_builder, "{sv}", "settings", gtk_print_settings_to_gvariant (handle->params->settings));
      g_variant_builder_add (&opt_builder, "{sv}", "page-setup", gtk_page_setup_to_gvariant (handle->params->page_setup));
      g_variant_builder_add (&opt_builder, "{sv}", "token", g_variant_new_uint32 (handle->params->token));
    }

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_print_complete_prepare_print (handle->impl,
                                         handle->invocation,
                                         handle->response,
                                         g_variant_builder_end (&opt_builder));

  print_dialog_handle_close (handle);
}

static void
handle_prepare_print_response (GtkDialog *dialog,
                               gint response,
                               gpointer data)
{
  PrintDialogHandle *handle = data;
  gboolean preview = FALSE;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      handle->response = 2;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      break;

    case GTK_RESPONSE_APPLY:
      preview = TRUE;
      /* fall thru */

    case GTK_RESPONSE_OK:
      {
        handle->response = 0;

        handle->params = print_params_new (handle->request->app_id,
                                           preview,
                                           gtk_print_unix_dialog_get_page_setup (GTK_PRINT_UNIX_DIALOG (handle->dialog)),
                                           gtk_print_unix_dialog_get_settings (GTK_PRINT_UNIX_DIALOG (handle->dialog)),
                                           gtk_print_unix_dialog_get_selected_printer (GTK_PRINT_UNIX_DIALOG (handle->dialog)));
      }
      break;
    }

  send_prepare_print_response (handle);
}

static gboolean
handle_prepare_print (XdpImplPrint *object,
                      GDBusMethodInvocation *invocation,
                      const char *arg_handle,
                      const char *arg_app_id,
                      const char *arg_parent_window,
                      const char *arg_title,
                      GVariant *arg_settings,
                      GVariant *arg_page_setup,
                      GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  GtkWidget *dialog;
  PrintDialogHandle *handle;
  GtkPrintSettings *settings;
  GtkPageSetup *page_setup;
  gboolean modal;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  if (arg_parent_window)
    {
      external_parent = create_external_window_from_handle (arg_parent_window);
      if (!external_parent)
        g_warning ("Failed to associate portal window with parent window %s",
                   arg_parent_window);
    }

  if (external_parent)
    display = external_window_get_display (external_parent);
  else
    display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);

  fake_parent = g_object_new (GTK_TYPE_WINDOW,
                              "type", GTK_WINDOW_TOPLEVEL,
                              "screen", screen,
                              NULL);
  g_object_ref_sink (fake_parent);

  settings = gtk_print_settings_new_from_gvariant (arg_settings);
  page_setup = gtk_page_setup_new_from_gvariant (arg_page_setup);
  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;

  dialog = gtk_print_unix_dialog_new (arg_title, NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), modal);
  gtk_print_unix_dialog_set_manual_capabilities (GTK_PRINT_UNIX_DIALOG (dialog),
                                                 can_preview () ? GTK_PRINT_CAPABILITY_PREVIEW : 0 |
						 GTK_PRINT_CAPABILITY_PAGE_SET |
						 GTK_PRINT_CAPABILITY_COPIES |
						 GTK_PRINT_CAPABILITY_COLLATE |
						 GTK_PRINT_CAPABILITY_REVERSE |
						 GTK_PRINT_CAPABILITY_SCALE |
						 GTK_PRINT_CAPABILITY_NUMBER_UP
						 );
  gtk_print_unix_dialog_set_embed_page_setup (GTK_PRINT_UNIX_DIALOG (dialog), TRUE);
  gtk_print_unix_dialog_set_settings (GTK_PRINT_UNIX_DIALOG (dialog), settings);
  gtk_print_unix_dialog_set_page_setup (GTK_PRINT_UNIX_DIALOG (dialog), page_setup);

  g_object_unref (settings);
  g_object_unref (page_setup);

  handle = g_new0 (PrintDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "response", G_CALLBACK (handle_prepare_print_response), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
print_init (GDBusConnection *bus,
            GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_print_skeleton_new ());

  g_signal_connect (helper, "handle-print", G_CALLBACK (handle_print), NULL);
  g_signal_connect (helper, "handle-prepare-print", G_CALLBACK (handle_prepare_print), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
