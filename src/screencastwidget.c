/*
 * Copyright © 2017-2018 Red Hat, Inc
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
#include <gtk/gtk.h>

#include "screencastwidget.h"
#include "displaystatetracker.h"

enum
{
  HAS_SELECTION_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _ScreenCastWidget
{
  GtkBox parent;

  GtkWidget *monitor_heading;
  GtkWidget *monitor_list;

  DisplayStateTracker *display_state_tracker;
  gulong monitors_changed_handler_id;
};

static GQuark quark_monitor_widget_data;

G_DEFINE_TYPE (ScreenCastWidget, screen_cast_widget, GTK_TYPE_BOX)

static GtkWidget *
create_monitor_widget (LogicalMonitor *logical_monitor)
{
  GtkWidget *monitor_widget;
  GList *l;

  monitor_widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (monitor_widget, 12);
  gtk_widget_set_margin_end (monitor_widget, 12);

  for (l = logical_monitor_get_monitors (logical_monitor); l; l = l->next)
    {
      Monitor *monitor = l->data;
      GtkWidget *monitor_label;

      if (!l->prev)
        g_object_set_qdata (G_OBJECT (monitor_widget),
                            quark_monitor_widget_data,
                            monitor);

      monitor_label = gtk_label_new (monitor_get_display_name (monitor));
      gtk_widget_set_margin_top (monitor_label, 12);
      gtk_widget_set_margin_bottom (monitor_label, 12);
      gtk_widget_show (monitor_label);
      gtk_container_add (GTK_CONTAINER (monitor_widget), monitor_label);
    }

  gtk_widget_show (monitor_widget);
  return monitor_widget;
}

static void
update_monitors_list (ScreenCastWidget *widget)
{
  GtkListBox *monitor_list = GTK_LIST_BOX (widget->monitor_list);
  GList *old_monitor_widgets;
  GList *logical_monitors;
  GList *l;

  old_monitor_widgets = gtk_container_get_children (GTK_CONTAINER (monitor_list));
  for (l = old_monitor_widgets; l; l = l->next)
    {
      GtkWidget *monitor_widget = l->data;

      gtk_container_remove (GTK_CONTAINER (monitor_list), monitor_widget);
    }
  g_list_free (old_monitor_widgets);

  logical_monitors =
    display_state_tracker_get_logical_monitors (widget->display_state_tracker);
  for (l = logical_monitors; l; l = l->next)
    {
      LogicalMonitor *logical_monitor = l->data;
      GtkWidget *monitor_widget;

      monitor_widget = create_monitor_widget (logical_monitor);
      gtk_container_add (GTK_CONTAINER (monitor_list), monitor_widget);
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
                  ScreenCastWidget *widget)
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
                          ScreenCastWidget *widget)
{
  GList *selected_rows;

  selected_rows = gtk_list_box_get_selected_rows (box);
  g_signal_emit (widget, signals[HAS_SELECTION_CHANGED], 0,
                 !!selected_rows);
  g_list_free (selected_rows);
}

static void
update_monitor_list_box_header (GtkListBoxRow *row,
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
on_monitors_changed (DisplayStateTracker *display_state_tracker,
                     ScreenCastWidget *widget)
{
  update_monitors_list (widget);
}

static gboolean
add_selections (ScreenCastWidget *widget,
                GVariantBuilder *source_selections_builder)
{
  GList *selected_rows;
  GList *l;

  selected_rows =
    gtk_list_box_get_selected_rows (GTK_LIST_BOX (widget->monitor_list));
  if (!selected_rows)
    return FALSE;

  for (l = selected_rows; l; l = l->next)
    {
      GtkWidget *monitor_widget = gtk_bin_get_child (l->data);
      Monitor *monitor;

      monitor = g_object_get_qdata (G_OBJECT (monitor_widget),
                                    quark_monitor_widget_data);

      g_variant_builder_add (source_selections_builder, "(us)",
                             SCREEN_CAST_SOURCE_TYPE_MONITOR,
                             monitor_get_connector (monitor));
    }
  g_list_free (selected_rows);

  return TRUE;
}

void
screen_cast_widget_add_selections (ScreenCastWidget *widget,
                                   GVariantBuilder *selections_builder)
{
  GVariantBuilder source_selections_builder;

  g_variant_builder_init (&source_selections_builder, G_VARIANT_TYPE ("a(us)"));
  if (!add_selections (widget, &source_selections_builder))
    {
      g_variant_builder_clear (&source_selections_builder);
    }
  else
    {
      g_variant_builder_add (selections_builder, "{sv}",
                             "selected_screen_cast_sources",
                             g_variant_builder_end (&source_selections_builder));
    }
}

void
screen_cast_widget_set_app_id (ScreenCastWidget *widget,
                               const char *app_id)
{
  g_autofree char *heading = NULL;

  if (app_id && strcmp (app_id, "") != 0)
    {
      g_autofree char *id = NULL;
      g_autoptr(GAppInfo) info = NULL;

      id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (id));
      heading = g_strdup_printf (_("Select monitor to share with %s"),
                                 g_app_info_get_display_name (info));
    }
  else
    {
      heading = g_strdup (_("Select monitor to share with the requesting application"));
    }

  gtk_label_set_label (GTK_LABEL (widget->monitor_heading), heading);
}

void
screen_cast_widget_set_allow_multiple (ScreenCastWidget *widget,
                                       gboolean multiple)
{
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (widget->monitor_list),
                                   multiple ? GTK_SELECTION_MULTIPLE
                                            : GTK_SELECTION_SINGLE);
}

static void
screen_cast_widget_finalize (GObject *object)
{
  ScreenCastWidget *widget = SCREEN_CAST_WIDGET (object);

  g_signal_handler_disconnect (widget->display_state_tracker,
                               widget->monitors_changed_handler_id);

  G_OBJECT_CLASS (screen_cast_widget_parent_class)->finalize (object);
}

static void
screen_cast_widget_init (ScreenCastWidget *widget)
{
  gtk_widget_init_template (GTK_WIDGET (widget));

  screen_cast_widget_set_app_id (widget, NULL);
  screen_cast_widget_set_allow_multiple (widget, FALSE);

  gtk_list_box_set_header_func (GTK_LIST_BOX (widget->monitor_list),
                                update_monitor_list_box_header,
                                NULL, NULL);
  g_signal_connect (widget->monitor_list, "row-activated",
                    G_CALLBACK (on_row_activated),
                    widget);
  g_signal_connect (widget->monitor_list, "selected-rows-changed",
                    G_CALLBACK (on_selected_rows_changed),
                    widget);

  widget->display_state_tracker = display_state_tracker_get ();
  widget->monitors_changed_handler_id =
    g_signal_connect (widget->display_state_tracker,
                      "monitors-changed",
                      G_CALLBACK (on_monitors_changed),
                      widget);

  update_monitors_list (widget);

  gtk_widget_show (widget->monitor_list);
}

static void
screen_cast_widget_class_init (ScreenCastWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = screen_cast_widget_finalize;

  signals[HAS_SELECTION_CHANGED] = g_signal_new ("has-selection-changed",
                                                 G_TYPE_FROM_CLASS (klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL, NULL,
                                                 NULL,
                                                 G_TYPE_NONE, 1,
                                                 G_TYPE_BOOLEAN);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/screencastwidget.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, monitor_heading);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, monitor_list);

  quark_monitor_widget_data = g_quark_from_static_string ("-monitor-widget-connector-quark");
}

void
init_screen_cast_widget (void)
{
  g_type_ensure (screen_cast_widget_get_type ());
}
