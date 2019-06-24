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
 */

#include "config.h"

#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#include "remotedesktop.h"
#include "remotedesktopdialog.h"
#include "screencastwidget.h"

struct _RemoteDesktopDialog
{
  GtkWindow parent;

  GtkWidget *accept_button;
  GtkWidget *screen_cast_widget;
  GtkWidget *device_heading;
  GtkWidget *device_list;

  RemoteDesktopDeviceType device_types;

  gboolean screen_cast_enable;
  ScreenCastSelection screen_cast;

  gboolean is_device_types_selected;
  gboolean is_screen_cast_sources_selected;
};

struct _RemoteDesktopDialogClass
{
  GtkWindowClass *parent_class;
};

enum
{
  DONE,

  N_SIGNAL
};

static guint signals[N_SIGNAL];

static GQuark quark_device_widget_data;

G_DEFINE_TYPE (RemoteDesktopDialog, remote_desktop_dialog, GTK_TYPE_WINDOW)

static void
add_device_type_selections (RemoteDesktopDialog *dialog,
                            GVariantBuilder *selections_builder)
{
  GList *selected_rows;
  GList *l;
  RemoteDesktopDeviceType selected_device_types = 0;

  selected_rows =
    gtk_list_box_get_selected_rows (GTK_LIST_BOX (dialog->device_list));
  for (l = selected_rows; l; l = l->next)
    {
      GtkWidget *device_type_widget = gtk_bin_get_child (l->data);

      selected_device_types |=
        GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (device_type_widget),
                                             quark_device_widget_data));
    }
  g_list_free (selected_rows);

  g_variant_builder_add (selections_builder, "{sv}",
                         "selected_device_types",
                         g_variant_new_uint32 (selected_device_types));
}

static void
button_clicked (GtkWidget *button,
                RemoteDesktopDialog *dialog)
{
  int response;
  GVariant *selections;

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (button == dialog->accept_button)
    {
      GVariantBuilder selections_builder;
      ScreenCastWidget *screen_cast_widget =
        SCREEN_CAST_WIDGET (dialog->screen_cast_widget);

      response = GTK_RESPONSE_OK;

      g_variant_builder_init (&selections_builder, G_VARIANT_TYPE_VARDICT);

      add_device_type_selections (dialog, &selections_builder);
      if (dialog->screen_cast_enable)
        screen_cast_widget_add_selections (screen_cast_widget,
                                           &selections_builder);
      selections = g_variant_builder_end (&selections_builder);
    }
  else
    {
      response = GTK_RESPONSE_CANCEL;
      selections = NULL;
    }

  g_signal_emit (dialog, signals[DONE], 0, response, selections);
}

static void
update_button_sensitivity (RemoteDesktopDialog *dialog)
{
  gboolean can_accept = FALSE;

  if (dialog->is_screen_cast_sources_selected)
    can_accept = TRUE;

  if (dialog->is_device_types_selected)
    can_accept = TRUE;

  if (can_accept)
    gtk_widget_set_sensitive (dialog->accept_button, TRUE);
  else
    gtk_widget_set_sensitive (dialog->accept_button, FALSE);
}

static GtkWidget *
create_device_type_widget (RemoteDesktopDeviceType device_type,
                           const char *name)
{
  GtkWidget *device_label;

  device_label = gtk_label_new (name);
  g_object_set_qdata (G_OBJECT (device_label), quark_device_widget_data,
                      GINT_TO_POINTER (device_type));
  gtk_widget_set_margin_top (device_label, 12);
  gtk_widget_set_margin_bottom (device_label, 12);
  gtk_widget_show (device_label);

  return device_label;
}

static void
update_device_list (RemoteDesktopDialog *dialog)
{
  GtkListBox *device_list = GTK_LIST_BOX (dialog->device_list);
  GList *old_device_type_widgets;
  GList *l;
  int n_device_types;
  int i;

  old_device_type_widgets =
    gtk_container_get_children (GTK_CONTAINER (device_list));
  for (l = old_device_type_widgets; l; l = l->next)
    {
      GtkWidget *device_type_widget = l->data;

      gtk_container_remove (GTK_CONTAINER (device_list), device_type_widget);
    }
  g_list_free (old_device_type_widgets);

  n_device_types = __builtin_popcount (REMOTE_DESKTOP_DEVICE_TYPE_ALL);
  for (i = 0; i < n_device_types; i++)
    {
      RemoteDesktopDeviceType device_type = 1 << i;
      const char *device_type_name = NULL;
      GtkWidget *device_type_widget;

      if (!(dialog->device_types & device_type))
        continue;

      switch (device_type)
        {
        case REMOTE_DESKTOP_DEVICE_TYPE_POINTER:
          device_type_name = _("Pointer");
          break;
        case REMOTE_DESKTOP_DEVICE_TYPE_KEYBOARD:
          device_type_name = _("Keyboard");
          break;
        case REMOTE_DESKTOP_DEVICE_TYPE_TOUCHSCREEN:
          device_type_name = _("Touch screen");
          break;
        case REMOTE_DESKTOP_DEVICE_TYPE_NONE:
        case REMOTE_DESKTOP_DEVICE_TYPE_ALL:
          g_assert_not_reached ();
        }

      device_type_widget = create_device_type_widget (device_type,
                                                      device_type_name);
      gtk_container_add (GTK_CONTAINER (device_list), device_type_widget);
    }
}

static gboolean
is_row_selected (GtkListBoxRow *row)
{
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row),
                                             "is-row-selected"));
}

static void
set_row_is_selected (GtkListBoxRow *row,
                     gboolean is_selected)
{
  g_object_set_data (G_OBJECT (row),
                     "is-row-selected",
                     GINT_TO_POINTER (is_selected));
}

static void
on_row_activated (GtkListBox *box,
                  GtkListBoxRow *row,
                  RemoteDesktopDialog *dialog)
{
  if (!row)
    return;

  if (is_row_selected (row))
    {
      set_row_is_selected (row, FALSE);
      gtk_list_box_unselect_row (box, row);
    }
  else
    {
      set_row_is_selected (row, TRUE);
      gtk_list_box_select_row (box, row);
    }
}

static void
on_selected_rows_changed (GtkListBox *box,
                          RemoteDesktopDialog *dialog)
{
  GList *selected_rows;

  selected_rows = gtk_list_box_get_selected_rows (box);
  dialog->is_device_types_selected = !!selected_rows;
  g_list_free (selected_rows);

  update_button_sensitivity (dialog);
}

static void
update_device_list_box_header (GtkListBoxRow *row,
                               GtkListBoxRow *before,
                               gpointer user_data)
{
  GtkWidget *header;

  if (before)
    header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  else
    header = NULL;

  gtk_list_box_row_set_header (row, header);
}

static void
on_has_selection_changed (ScreenCastWidget *screen_cast_widget,
                          gboolean has_selection,
                          RemoteDesktopDialog *dialog)
{
  dialog->is_screen_cast_sources_selected = has_selection;
  update_button_sensitivity (dialog);
}

RemoteDesktopDialog *
remote_desktop_dialog_new (const char *app_id,
                           RemoteDesktopDeviceType device_types,
                           ScreenCastSelection *screen_cast_select)
{
  RemoteDesktopDialog *dialog;
  g_autofree char *heading = NULL;

  dialog = g_object_new (REMOTE_DESKTOP_TYPE_DIALOG, NULL);
  dialog->device_types = device_types;
  if (screen_cast_select)
    {
      dialog->screen_cast_enable = TRUE;
      dialog->screen_cast = *screen_cast_select;
    }
  else
    {
      dialog->screen_cast_enable = FALSE;
    }

  if (g_strcmp0 (app_id, "") != 0)
    {
      g_autofree char *id = NULL;
      g_autoptr(GAppInfo) info = NULL;

      id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (id));
      heading = g_strdup_printf (_("Select devices to share with %s"),
                                 g_app_info_get_display_name (info));
    }
  else
    {
      heading = g_strdup (_("Select devices to share with the requesting application"));
    }

  if (dialog->screen_cast_enable)
    {
      ScreenCastWidget *screen_cast_widget =
        SCREEN_CAST_WIDGET (dialog->screen_cast_widget);

      screen_cast_widget_set_allow_multiple (screen_cast_widget,
                                             screen_cast_select->multiple);
      screen_cast_widget_set_source_types (screen_cast_widget,
                                           screen_cast_select->source_types);

      g_signal_connect (screen_cast_widget, "has-selection-changed",
                        G_CALLBACK (on_has_selection_changed), dialog);
      gtk_widget_show (GTK_WIDGET (screen_cast_widget));
    }

  gtk_label_set_label (GTK_LABEL (dialog->device_heading), heading);

  gtk_list_box_set_selection_mode (GTK_LIST_BOX (dialog->device_list),
                                   GTK_SELECTION_MULTIPLE);
  gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->device_list),
                                update_device_list_box_header,
                                NULL, NULL);

  g_signal_connect (dialog->device_list, "row-activated",
                    G_CALLBACK (on_row_activated),
                    dialog);
  g_signal_connect (dialog->device_list, "selected-rows-changed",
                    G_CALLBACK (on_selected_rows_changed),
                    dialog);

  update_device_list (dialog);

  gtk_widget_show (dialog->device_list);

  return dialog;
}

static void
remote_desktop_dialog_init (RemoteDesktopDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static gboolean
remote_desktop_dialog_delete_event (GtkWidget *dialog, GdkEventAny *event)
{
  gtk_widget_hide (dialog);

  g_signal_emit (dialog, signals[DONE], 0, GTK_RESPONSE_CANCEL, NULL);

  return TRUE;
}

static void
remote_desktop_dialog_class_init (RemoteDesktopDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->delete_event = remote_desktop_dialog_delete_event;

  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 2,
                                G_TYPE_INT,
                                G_TYPE_VARIANT);

  init_screen_cast_widget ();

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/remotedesktopdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, RemoteDesktopDialog, accept_button);
  gtk_widget_class_bind_template_child (widget_class, RemoteDesktopDialog, screen_cast_widget);
  gtk_widget_class_bind_template_child (widget_class, RemoteDesktopDialog, device_heading);
  gtk_widget_class_bind_template_child (widget_class, RemoteDesktopDialog, device_list);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked);

  quark_device_widget_data = g_quark_from_static_string ("-device-widget-type-quark");
}
