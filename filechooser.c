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

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "filechooser.h"
#include "xdg-desktop-portal-gtk.h"

typedef struct {
  DialogHandle base;

  GtkWidget *dialog;
  GtkFileChooserAction action;
  gboolean multiple;

  int response;
  GSList *uris;

  gboolean allow_write;

  GHashTable *choices;
} FileDialogHandle;

static FileDialogHandle *
dialog_handle_new (const char *app_id,
                   const char *sender,
                   GtkWidget *dialog,
                   GDBusInterfaceSkeleton *skeleton)
{
  FileDialogHandle *handle = g_new0 (FileDialogHandle, 1);

  handle->base.app_id = g_strdup (app_id);
  handle->base.sender = g_strdup (sender);
  handle->base.skeleton = g_object_ref (skeleton);

  handle->dialog = g_object_ref (dialog);
  handle->allow_write = TRUE;
  handle->choices = g_hash_table_new (g_str_hash, g_str_equal);

  dialog_handle_register (&handle->base);

  /* TODO: Track lifetime of sender and close handle */

  return handle;
}

static void
file_dialog_handle_free (FileDialogHandle *handle)
{
  dialog_handle_unregister (&handle->base);
  g_free (handle->base.id);
  g_free (handle->base.app_id);
  g_free (handle->base.sender);
  g_object_unref (handle->base.skeleton);
  g_object_unref (handle->dialog);

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
dialog_handler_emit_response (FileDialogHandle *handle,
                              const char *interface,
                              const char *signal,
                              GVariant *arguments)
{
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (handle->base.skeleton),
                                 "org.freedesktop.portal.Desktop",
                                 "/org/freedesktop/portal/desktop",
                                 interface, signal, arguments, NULL);
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
send_response (FileDialogHandle *handle)
{
  GVariantBuilder opt_builder;
  GVariant *options;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}", "writable", g_variant_new_variant (g_variant_new_boolean (handle->allow_write)));

  add_choices (handle, &opt_builder);

  options = g_variant_builder_end (&opt_builder);

  if (handle->action == GTK_FILE_CHOOSER_ACTION_SAVE ||
      (handle->action == GTK_FILE_CHOOSER_ACTION_OPEN && !handle->multiple))
    {
      const char *signal_name;

      if (handle->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        signal_name = "SaveFileResponse";
      else
        signal_name = "OpenFileResponse";

      dialog_handler_emit_response (handle,
                                    "org.freedesktop.impl.portal.FileChooser",
                                    signal_name,
                                    g_variant_new ("(sous@a{sv})",
                                                   handle->base.sender,
                                                   handle->base.id,
                                                   handle->response,
                                                   handle->uris ? (char *)handle->uris->data : "",
                                                   options));
    }
  else
    {
      g_auto(GStrv) uris = NULL;
      GSList *l;
      gint i;

      uris = g_new (char *, g_slist_length (handle->uris) + 1);
      for (l = handle->uris, i = 0; l; l = l->next)
        uris[i++] = l->data;
      uris[i] = NULL;

      g_slist_free (handle->uris);
      handle->uris = NULL;

      dialog_handler_emit_response (handle,
                                    "org.freedesktop.impl.portal.FileChooser",
                                    "OpenFilesResponse",
                                    g_variant_new ("(sou^as@a{sv})",
                                                   handle->base.sender,
                                                   handle->base.id,
                                                   handle->response,
                                                   uris,
                                                   options));
    }

  file_dialog_handle_close (handle);
}

GtkFileFilter *
gtk_file_filter_from_gvariant (GVariant *variant)
{
  GtkFileFilter *filter;
  GVariantIter *iter;
  const char *name;
  int type;
  char *tmp;

  filter = gtk_file_filter_new ();

  g_variant_get (variant, "(&sa(us))", &name, &iter);

  gtk_file_filter_set_name (filter, name);

  while (g_variant_iter_next (iter, "(u&s)", &type, &tmp))
    {
      switch (type)
        {
        case 0:
          gtk_file_filter_add_pattern (filter, tmp);
          break;
        case 1:
          gtk_file_filter_add_mime_type (filter, tmp);
          break;
        default:
          break;
       }
    }
  g_variant_iter_free (iter);

  return filter;
}

static void
handle_file_chooser_open_response (GtkWidget *widget,
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
      handle->uris = NULL;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      handle->uris = NULL;
      break;

    case GTK_RESPONSE_OK:
      handle->response = 0;
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
handle_file_chooser_open (XdpFileChooser *object,
                          GDBusMethodInvocation *invocation,
                          const gchar *arg_sender,
                          const gchar *arg_app_id,
                          const gchar *arg_parent_window,
                          const gchar *arg_title,
                          GVariant *arg_options)
{
  const gchar *method_name;
  GtkFileChooserAction action;
  gboolean multiple;
  GtkWidget *dialog;
  GdkWindow *foreign_parent = NULL;
  GtkWidget *fake_parent;
  FileDialogHandle *handle;
  XdpFileChooser *chooser = XDP_FILE_CHOOSER (g_dbus_method_invocation_get_user_data (invocation));
  const char *cancel_label;
  const char *accept_label;
  GVariantIter *iter;
  const char *current_name;
  const char *path;
  g_autoptr (GVariant) choices = NULL;

  method_name = g_dbus_method_invocation_get_method_name (invocation);

  g_print ("%s, app_id: %s, object: %p, user_data: %p\n",
           method_name, arg_app_id, object,
           g_dbus_method_invocation_get_user_data (invocation));

  fake_parent = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_object_ref_sink (fake_parent);

  action = GTK_FILE_CHOOSER_ACTION_OPEN;
  multiple = FALSE;

  if (strcmp (method_name, "SaveFile") == 0)
    action = GTK_FILE_CHOOSER_ACTION_SAVE;
  else if (strcmp (method_name, "OpenFiles") == 0)
    multiple = TRUE;

  if (!g_variant_lookup (arg_options, "accept_label", "&s", &accept_label))
    accept_label = _("_Open");

  cancel_label = _("_Cancel");

  dialog = gtk_file_chooser_dialog_new (arg_title, GTK_WINDOW (fake_parent), action,
                                        cancel_label, GTK_RESPONSE_CANCEL,
                                        accept_label, GTK_RESPONSE_OK,
                                        NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), multiple);

  handle = dialog_handle_new (arg_app_id, arg_sender, dialog, G_DBUS_INTERFACE_SKELETON (chooser));

  handle->dialog = dialog;
  handle->action = action;
  handle->multiple = multiple;

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

          filter = gtk_file_filter_from_gvariant (variant);
          gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
          g_variant_unref (variant);
        }
      g_variant_iter_free (iter);
    }
  if (strcmp (method_name, "SaveFile") == 0)
    {
      if (g_variant_lookup (arg_options, "current_name", "&s", &current_name))
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), current_name);
      /* TODO: is this useful ?
       * In a sandboxed situation, the current folder and current file
       * are likely in the fuse filesystem
       */
      if (g_variant_lookup (arg_options, "current_folder", "&ay", &path))
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), path);
      if (g_variant_lookup (arg_options, "current_file", "&ay", &path))
        gtk_file_chooser_select_filename (GTK_FILE_CHOOSER (dialog), path);
    }

  g_object_unref (fake_parent);

#ifdef GDK_WINDOWING_X11
  if (g_str_has_prefix (arg_parent_window, "x11:"))
    {
      int xid;

      if (sscanf (arg_parent_window, "x11:%x", &xid) != 1)
        g_warning ("invalid xid");
      else
        foreign_parent = gdk_x11_window_foreign_new_for_display (gtk_widget_get_display (dialog), xid);
    }
#endif
  else
    g_warning ("Unhandled parent window type %s\n", arg_parent_window);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

  g_signal_connect (G_OBJECT (dialog), "response",
                    G_CALLBACK (handle_file_chooser_open_response), handle);

  if (action == GTK_FILE_CHOOSER_ACTION_OPEN)
    {
      GtkWidget *readonly;

      readonly = gtk_check_button_new_with_label ("Open files read-only");
      gtk_widget_show (readonly);

      g_signal_connect (readonly, "toggled",
                        G_CALLBACK (read_only_toggled), handle);

      gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), readonly);
    }

  gtk_widget_realize (dialog);

  if (foreign_parent)
    gdk_window_set_transient_for (gtk_widget_get_window (dialog), foreign_parent);

  gtk_widget_show (dialog);

  xdp_file_chooser_complete_open_file (chooser,
                                       invocation,
                                       handle->base.id);

  return TRUE;
}

static gboolean
handle_file_chooser_close (XdpFileChooser *object,
                           GDBusMethodInvocation *invocation,
                           const gchar *arg_sender,
                           const gchar *arg_app_id,
                           const gchar *arg_handle)
{
  FileDialogHandle *handle;

  handle = (FileDialogHandle *)dialog_handle_find (arg_sender, arg_app_id, arg_handle,
                                                   XDP_TYPE_FILE_CHOOSER_SKELETON);

  if (handle != NULL)
    {
      file_dialog_handle_close (handle);
      xdp_file_chooser_complete_close (object, invocation);
    }
  else
    {
      g_dbus_method_invocation_return_dbus_error (invocation,
                                                  "org.freedesktop.Flatpak.Error.NotFound",
                                                  "No such handle");
    }

  return TRUE;
}

gboolean
file_chooser_init (GDBusConnection *bus,
                   GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_file_chooser_skeleton_new ());

  g_signal_connect (helper, "handle-open-file", G_CALLBACK (handle_file_chooser_open), NULL);
  g_signal_connect (helper, "handle-open-files", G_CALLBACK (handle_file_chooser_open), NULL);
  g_signal_connect (helper, "handle-save-file", G_CALLBACK (handle_file_chooser_open), NULL);
  g_signal_connect (helper, "handle-close", G_CALLBACK (handle_file_chooser_close), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         "/org/freedesktop/portal/desktop",
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
