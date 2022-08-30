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

#include "xdg-desktop-portal-dbus.h"

#include "filechooser.h"
#include "request.h"
#include "utils.h"
#include "gtkbackports.h"
#include "externalwindow.h"


typedef struct {
  XdpImplFileChooser *impl;
  GDBusMethodInvocation *invocation;
  Request *request;
  GtkWidget *dialog;
  GtkFileChooserAction action;
  gboolean multiple;
  ExternalWindow *external_parent;

  GSList *files;

  GtkFileFilter *filter;

  int response;
  GSList *uris;

  gboolean allow_write;

  GHashTable *choices;
} FileDialogHandle;

static void
file_dialog_handle_free (gpointer data)
{
  FileDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->dialog);
  g_object_unref (handle->request);
  g_slist_free_full (handle->files, g_free);
  g_slist_free_full (handle->uris, g_free);
  g_hash_table_unref (handle->choices);

  g_free (handle);
}

static void
file_dialog_handle_close (FileDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  file_dialog_handle_free (handle);
}

static void
add_choices (FileDialogHandle *handle,
             GVariantBuilder *builder)
{
  GVariantBuilder choices;
  GHashTableIter iter;
  const char *id;
  const char *selected;

  g_variant_builder_init (&choices, G_VARIANT_TYPE ("a(ss)"));
  g_hash_table_iter_init (&iter, handle->choices);
  while (g_hash_table_iter_next (&iter, (gpointer *)&id, (gpointer *)&selected))
    g_variant_builder_add (&choices, "(ss)", id, selected);

  g_variant_builder_add (builder, "{sv}", "choices", g_variant_builder_end (&choices));
}

static void
add_recent_entry (const char *app_id,
                  const char *uri)
{
  GtkRecentManager *recent;
  GtkRecentData data;

  /* These fields are ignored by everybody, so it is not worth
   * spending effort on filling them out. Just use defaults.
   */
  data.display_name = NULL;
  data.description = NULL;
  data.mime_type = "application/octet-stream";
  data.app_name = (char *)app_id;
  data.app_exec = "gio open %u";
  data.groups = NULL;
  data.is_private = FALSE;

  recent = gtk_recent_manager_get_default ();
  gtk_recent_manager_add_full (recent, uri, &data);
}

static void
send_response (FileDialogHandle *handle)
{
  GVariantBuilder uri_builder;
  GVariantBuilder opt_builder;
  GSList *l;
  const char *method_name;

  method_name = g_dbus_method_invocation_get_method_name (handle->invocation);

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (strcmp (method_name, "SaveFiles") == 0 &&
      handle->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER &&
      handle->uris)
    {
      g_autoptr(GFile) base_dir = g_file_new_for_uri (handle->uris->data);

      g_slist_free_full (handle->uris, g_free);
      handle->uris = NULL;

      for (l = handle->files; l; l = l->next)
        {
          int uniqifier = 0;
          const char *file_name = l->data;
          g_autoptr(GFile) file = g_file_get_child (base_dir, file_name);

          while (g_file_query_exists(file, NULL))
            {
              g_autofree char *base_name = g_file_get_basename (file);
              g_auto(GStrv) parts = NULL;
              g_autoptr(GString) unique_name = NULL;

              parts = g_strsplit (base_name, ".", 2);

              unique_name = g_string_new (parts[0]);
              g_string_append_printf (unique_name, "(%i)", ++uniqifier);
              if (parts[1] != NULL)
                  g_string_append (unique_name, parts[1]);

              file = g_file_get_child (base_dir, unique_name->str);
            }
          handle->uris = g_slist_append (handle->uris, g_file_get_uri (file));
        }
    }

  g_variant_builder_init (&uri_builder, G_VARIANT_TYPE_STRING_ARRAY);
  for (l = handle->uris; l; l = l->next)
    {
      add_recent_entry (handle->request->app_id, l->data);
      g_variant_builder_add (&uri_builder, "s", l->data);
    }

  if (handle->filter) {
    GVariant *current_filter_variant = gtk_file_filter_to_gvariant (handle->filter);
    g_variant_builder_add (&opt_builder, "{sv}", "current_filter", current_filter_variant);
  }

  g_variant_builder_add (&opt_builder, "{sv}", "uris", g_variant_builder_end (&uri_builder));
  g_variant_builder_add (&opt_builder, "{sv}", "writable", g_variant_new_boolean (handle->allow_write));

  add_choices (handle, &opt_builder);

  if (handle->request->exported)
    request_unexport (handle->request);

  if (strcmp (method_name, "OpenFile") == 0)
    xdp_impl_file_chooser_complete_open_file (handle->impl,
                                              handle->invocation,
                                              handle->response,
                                              g_variant_builder_end (&opt_builder));
  else if (strcmp (method_name, "SaveFile") == 0)
    xdp_impl_file_chooser_complete_save_file (handle->impl,
                                              handle->invocation,
                                              handle->response,
                                              g_variant_builder_end (&opt_builder));
  else if (strcmp (method_name, "SaveFiles") == 0)
    xdp_impl_file_chooser_complete_save_files (handle->impl,
                                               handle->invocation,
                                               handle->response,
                                               g_variant_builder_end (&opt_builder));
  else
    g_assert_not_reached ();

  file_dialog_handle_close (handle);
}

static void
file_chooser_response (GtkWidget *widget,
                       int response,
                       gpointer user_data)
{
  FileDialogHandle *handle = user_data;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      handle->response = 2;
      handle->filter = NULL;
      handle->uris = NULL;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      handle->filter = NULL;
      handle->uris = NULL;
      break;

    case GTK_RESPONSE_OK:
      handle->response = 0;
      handle->filter = gtk_file_chooser_get_filter (GTK_FILE_CHOOSER(widget));
      handle->uris = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (widget));
      break;
    }

  send_response (handle);
}

static void
read_only_toggled (GtkToggleButton *button, gpointer user_data)
{
  FileDialogHandle *handle = user_data;

  handle->allow_write = !gtk_toggle_button_get_active (button);
}

static void
choice_changed (GtkComboBox *combo,
                FileDialogHandle *handle)
{
  const char *id;
  const char *selected;

  id = (const char *)g_object_get_data (G_OBJECT (combo), "choice-id");
  selected = gtk_combo_box_get_active_id (combo);
  g_hash_table_replace (handle->choices, (gpointer)id, (gpointer)selected);
}

static void
choice_toggled (GtkToggleButton *toggle,
                FileDialogHandle *handle)
{
  const char *id;
  const char *selected;

  id = (const char *)g_object_get_data (G_OBJECT (toggle), "choice-id");
  selected = gtk_toggle_button_get_active (toggle) ? "true" : "false";
  g_hash_table_replace (handle->choices, (gpointer)id, (gpointer)selected);
}

static GtkWidget *
deserialize_choice (GVariant *choice,
                    FileDialogHandle *handle)
{
  GtkWidget *widget;
  const char *choice_id;
  const char *label;
  const char *selected;
  GVariant *choices;
  int i;

  g_variant_get (choice, "(&s&s@a(ss)&s)", &choice_id, &label, &choices, &selected);

  if (g_variant_n_children (choices) > 0)
    {
      GtkWidget *box;
      GtkWidget *combo;

      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_container_add (GTK_CONTAINER (box), gtk_label_new (label));

      combo = gtk_combo_box_text_new ();
      g_object_set_data_full (G_OBJECT (combo), "choice-id", g_strdup (choice_id), g_free);
      gtk_container_add (GTK_CONTAINER (box), combo);

      for (i = 0; i < g_variant_n_children (choices); i++)
        {
          const char *id;
          const char *text;

          g_variant_get_child (choices, i, "(&s&s)", &id, &text);
          gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), id, text);
        }

      if (strcmp (selected, "") == 0)
        g_variant_get_child (choices, 0, "(&s&s)", &selected, NULL);

      g_signal_connect (combo, "changed", G_CALLBACK (choice_changed), handle);
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), selected);

      widget = box;
    }
  else
    {
      GtkWidget *check;

      check = gtk_check_button_new_with_label (label);
      g_object_set_data_full (G_OBJECT (check), "choice-id", g_strdup (choice_id), g_free);
      g_signal_connect (check, "toggled", G_CALLBACK (choice_toggled), handle);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), g_strcmp0 (selected, "true") == 0);

      widget = check;
    }

  gtk_widget_show_all (widget);

  return widget;
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              FileDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (handle->action == GTK_FILE_CHOOSER_ACTION_OPEN)
    xdp_impl_file_chooser_complete_open_file (handle->impl,
                                              handle->invocation,
                                              2,
                                              g_variant_builder_end (&opt_builder));
  else if (handle->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    xdp_impl_file_chooser_complete_save_file (handle->impl,
                                              handle->invocation,
                                              2,
                                              g_variant_builder_end (&opt_builder));
  else
    xdp_impl_file_chooser_complete_save_files (handle->impl,
                                               handle->invocation,
                                               2,
                                               g_variant_builder_end (&opt_builder));

  if (handle->request->exported)
    request_unexport (handle->request);

  file_dialog_handle_close (handle);

  xdp_impl_request_complete_close (object, invocation);

  return TRUE;
}

static void
update_preview_cb (GtkFileChooser *file_chooser, gpointer data)
{
  GtkWidget *preview = GTK_WIDGET (data);
  g_autofree char *filename = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;

  filename = gtk_file_chooser_get_preview_filename (file_chooser);
  if (filename)
    pixbuf = gdk_pixbuf_new_from_file_at_size (filename, 128, 128, NULL);

  if (pixbuf)
    {
      g_autoptr(GdkPixbuf) tmp = NULL;

      tmp = gdk_pixbuf_apply_embedded_orientation (pixbuf);
      g_set_object (&pixbuf, tmp);
    }

  gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
  gtk_file_chooser_set_preview_widget_active (file_chooser, pixbuf != NULL);
}

static gboolean
handle_open (XdpImplFileChooser *object,
             GDBusMethodInvocation *invocation,
             const char *arg_handle,
             const char *arg_app_id,
             const char *arg_parent_window,
             const char *arg_title,
             GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const gchar *method_name;
  const gchar *sender;
  GtkFileChooserAction action;
  gboolean multiple;
  gboolean directory;
  gboolean modal;
  GdkDisplay *display;
  GdkScreen *screen;
  GtkWidget *dialog;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;
  FileDialogHandle *handle;
  const char *cancel_label;
  const char *accept_label;
  GVariantIter *iter;
  const char *current_name;
  const char *path;
  g_autoptr (GVariant) choices = NULL;
  g_autoptr (GVariant) current_filter = NULL;
  GSList *filters = NULL;
  GtkWidget *preview;

  method_name = g_dbus_method_invocation_get_method_name (invocation);
  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  if (strcmp (method_name, "SaveFile") == 0)
    {
      action = GTK_FILE_CHOOSER_ACTION_SAVE;
      multiple = FALSE;
    }
  else if (strcmp (method_name, "SaveFiles") == 0)
    {
      action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
      multiple = FALSE;
    }
  else
    {
      if (!g_variant_lookup (arg_options, "multiple", "b", &multiple))
        multiple = FALSE;
      if (!g_variant_lookup (arg_options, "directory", "b", &directory))
        directory = FALSE;
      action = directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
    }

  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;

  if (!g_variant_lookup (arg_options, "accept_label", "&s", &accept_label))
    {
      if (strcmp (method_name, "OpenFile") == 0)
        accept_label = multiple ? _("_Open") : _("_Select");
      else
        accept_label = _("_Save");
    }

  cancel_label = _("_Cancel");

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

  dialog = gtk_file_chooser_dialog_new (arg_title, GTK_WINDOW (fake_parent), action,
                                        cancel_label, GTK_RESPONSE_CANCEL,
                                        accept_label, GTK_RESPONSE_OK,
                                        NULL);
  gtk_window_set_modal (GTK_WINDOW (dialog), modal);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), multiple);

  preview = gtk_image_new ();
  g_object_set (preview, "margin", 10, NULL);
  gtk_widget_show (preview);
  gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (dialog), preview);
  gtk_file_chooser_set_preview_widget_active (GTK_FILE_CHOOSER (dialog), FALSE);
  gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (dialog), FALSE);
  g_signal_connect (dialog, "update-preview", G_CALLBACK (update_preview_cb), preview);

  handle = g_new0 (FileDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->action = action;
  handle->multiple = multiple;
  handle->choices = g_hash_table_new (g_str_hash, g_str_equal);
  handle->external_parent = external_parent;
  handle->allow_write = TRUE;

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "response", G_CALLBACK (file_chooser_response), handle);

  choices = g_variant_lookup_value (arg_options, "choices", G_VARIANT_TYPE ("a(ssa(ss)s)"));
  if (choices)
    {
      int i;
      GtkWidget *box;

      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_widget_show (box);
      for (i = 0; i < g_variant_n_children (choices); i++)
        {
          GVariant *value = g_variant_get_child_value (choices, i);
          gtk_container_add (GTK_CONTAINER (box), deserialize_choice (value, handle));
        }

      gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), box);
    }

  if (g_variant_lookup (arg_options, "filters", "a(sa(us))", &iter))
    {
      GVariant *variant;

      while (g_variant_iter_next (iter, "@(sa(us))", &variant))
        {
          GtkFileFilter *filter;

          filter = gtk_file_filter_new_from_gvariant (variant);
          filters = g_slist_append (filters, g_object_ref (filter));
          gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
          g_variant_unref (variant);
        }
      g_variant_iter_free (iter);
    }

  if (g_variant_lookup (arg_options, "current_filter", "@(sa(us))", &current_filter))
    {
      g_autoptr (GtkFileFilter) filter = NULL;
      const char *current_filter_name;

      filter = gtk_file_filter_new_from_gvariant (current_filter);
      current_filter_name = gtk_file_filter_get_name (filter);

      if (!filters)
        {
          /* We are setting a single, unchangeable filter. */
          gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);
        }
      else
        {
          gboolean handled = FALSE;

          /* We are trying to select the default filter from the list of
           * filters. We want to naively take filter and pass it to
           * gtk_file_chooser_set_filter(), but it's not good enough
           * because GTK just compares filters by pointer value, so the
           * pointer itself has to match. We'll use the heuristic that
           * if two filters have the same name, they must be the same
           * unless the application is very dumb.
           */
          for (GSList *l = filters; l; l = l->next)
            {
              GtkFileFilter *f = l->data;
              const char *name = gtk_file_filter_get_name (f);

              if (g_strcmp0 (name, current_filter_name) == 0)
                {
                  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), f);
                  handled = TRUE;
                  break;
                }
            }

          if (!handled)
            g_warning ("current file filter must be present in filters list when list is nonempty");
        }
    }
  g_slist_free_full (filters, g_object_unref);

  if (strcmp (method_name, "OpenFile") == 0)
    {
      if (g_variant_lookup (arg_options, "current_folder", "^&ay", &path))
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), path);
    }
  else if (strcmp (method_name, "SaveFile") == 0)
    {
      if (g_variant_lookup (arg_options, "current_name", "&s", &current_name))
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), current_name);
      /* TODO: is this useful ?
       * In a sandboxed situation, the current folder and current file
       * are likely in the fuse filesystem
       */
      if (g_variant_lookup (arg_options, "current_folder", "^&ay", &path))
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), path);
      if (g_variant_lookup (arg_options, "current_file", "^&ay", &path))
        gtk_file_chooser_select_filename (GTK_FILE_CHOOSER (dialog), path);
    }
  else if (strcmp (method_name, "SaveFiles") == 0)
    {
      /* TODO: is this useful ?
       * In a sandboxed situation, the current folder and current file
       * are likely in the fuse filesystem
       */
      if (g_variant_lookup (arg_options, "current_folder", "^&ay", &path))
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), path);

      if (g_variant_lookup (arg_options, "files", "aay", &iter))
        {
          char *file = NULL;
          while (g_variant_iter_next (iter, "^ay", &file))
            handle->files = g_slist_append (handle->files, file);

          g_variant_iter_free (iter);
        }
    }

  g_object_unref (fake_parent);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

  if (action == GTK_FILE_CHOOSER_ACTION_OPEN)
    {
      GtkWidget *readonly;
      GtkWidget *extra;

      extra = gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog));

      readonly = gtk_check_button_new_with_label (_("Open files read-only"));
      gtk_widget_show (readonly);

      g_signal_connect (readonly, "toggled",
                        G_CALLBACK (read_only_toggled), handle);

      if (GTK_IS_CONTAINER (extra))
        gtk_container_add (GTK_CONTAINER (extra), readonly);
      else
        gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), readonly);
    }

  gtk_widget_show (dialog);
  gtk_window_present (GTK_WINDOW (dialog));

  if (external_parent)
    external_window_set_parent_of (external_parent,
                                   gtk_widget_get_window (dialog));


  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
file_chooser_init (GDBusConnection *bus,
                   GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_file_chooser_skeleton_new ());

  g_signal_connect (helper, "handle-open-file", G_CALLBACK (handle_open), NULL);
  g_signal_connect (helper, "handle-save-file", G_CALLBACK (handle_open), NULL);
  g_signal_connect (helper, "handle-save-files", G_CALLBACK (handle_open), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
