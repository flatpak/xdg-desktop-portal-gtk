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
#include "shellintrospect.h"

enum
{
  HAS_SELECTION_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _ScreenCastWidget
{
  GtkBox parent;

  GtkWidget *source_type_switcher;
  GtkWidget *source_type;
  GtkWidget *window_selection;
  GtkWidget *monitor_selection;

  GtkWidget *monitor_heading;
  GtkWidget *monitor_list;

  GtkWidget *window_heading;
  GtkWidget *window_list;
  GtkWidget *window_list_scrolled;

  DisplayStateTracker *display_state_tracker;
  gulong monitors_changed_handler_id;

  ShellIntrospect *shell_introspect;
  gulong windows_changed_handler_id;

  guint selection_changed_timeout_id;
};

static GQuark quark_monitor_widget_data;
static GQuark quark_window_widget_data;

G_DEFINE_TYPE (ScreenCastWidget, screen_cast_widget, GTK_TYPE_BOX)

static GtkWidget *
create_window_widget (Window *window)
{
  GtkWidget *window_widget;
  GtkWidget *window_label;
  GtkWidget *window_image;
  GIcon *icon = NULL;
  g_autoptr(GDesktopAppInfo) info = NULL;

  window_widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start (window_widget, 12);
  gtk_widget_set_margin_end (window_widget, 12);
  info = g_desktop_app_info_new (window_get_app_id (window));
  if (info != NULL)
    icon = g_app_info_get_icon (G_APP_INFO (info));
  if (icon == NULL)
    icon = g_themed_icon_new ("application-x-executable");
  window_image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DND);
  gtk_image_set_pixel_size (GTK_IMAGE (window_image), 32);
  gtk_widget_set_margin_start (window_image, 12);
  gtk_widget_set_margin_end (window_image, 12);
  gtk_widget_show (window_image);

  gtk_container_add (GTK_CONTAINER (window_widget), window_image);

  window_label = gtk_label_new (window_get_title (window));
  gtk_widget_set_margin_top (window_label, 12);
  gtk_widget_set_margin_bottom (window_label, 12);
  gtk_widget_show (window_label);
  gtk_container_add (GTK_CONTAINER (window_widget), window_label);

  g_object_set_qdata (G_OBJECT (window_widget),
                      quark_window_widget_data,
                      window);
  gtk_widget_show (window_widget);
  return window_widget;
}

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

static gboolean
should_skip_window (Window *window,
                    GtkWindow *toplevel)
{
  g_autofree char *processed_app_id = NULL;

  if (g_strcmp0 (window_get_title (window),
                 gtk_window_get_title (toplevel)) != 0)
    return FALSE;

  processed_app_id = g_strdup (window_get_app_id (window));
  if (g_str_has_suffix (processed_app_id, ".desktop"))
    processed_app_id[strlen (processed_app_id) -
                     strlen (".desktop")] = '\0';

  if (g_strcmp0 (processed_app_id, g_get_prgname ()) != 0 &&
      g_strcmp0 (processed_app_id, gdk_get_program_class ()) != 0)
    return FALSE;

  return TRUE;
}

static void
update_windows_list (ScreenCastWidget *widget)
{
  GtkListBox *window_list = GTK_LIST_BOX (widget->window_list);
  GList *old_window_widgets;
  GtkWidget *toplevel;
  GList *windows;
  GList *l;

  old_window_widgets = gtk_container_get_children (GTK_CONTAINER (window_list));
  for (l = old_window_widgets; l; l = l->next)
    {
      GtkWidget *window_widget = l->data;

      gtk_container_remove (GTK_CONTAINER (window_list), window_widget);
    }
  g_list_free (old_window_widgets);

  toplevel = gtk_widget_get_ancestor (GTK_WIDGET (widget), GTK_TYPE_WINDOW);
  if (!toplevel)
    return;

  windows = shell_introspect_get_windows (widget->shell_introspect);
  for (l = windows; l; l = l->next)
    {
      Window *window = l->data;
      GtkWidget *window_widget;

      if (should_skip_window (window, GTK_WINDOW (toplevel)))
        continue;

      window_widget = create_window_widget (window);
      gtk_container_add (GTK_CONTAINER (window_list), window_widget);
    }
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
unselect_row_cb (GtkWidget *widget,
                 gpointer user_data)
{
  GtkListBoxRow *row = GTK_LIST_BOX_ROW (widget);
  GtkListBox *box = (GtkListBox *)user_data;

  set_row_is_selected (row, FALSE);
  gtk_list_box_unselect_row (box, row);
}

static void
on_windows_changed (ShellIntrospect *shell_introspect,
                    ScreenCastWidget *widget)
{
  update_windows_list (widget);
}

static void
connect_windows_changed_listener (ScreenCastWidget *widget)
{
  g_assert (!widget->windows_changed_handler_id);
  widget->windows_changed_handler_id =
    g_signal_connect (widget->shell_introspect,
                          "windows-changed",
                          G_CALLBACK (on_windows_changed),
                          widget);
  shell_introspect_ref_listeners (widget->shell_introspect);
}

static void
disconnect_windows_changed_listener (ScreenCastWidget *widget)
{
  g_assert (widget->windows_changed_handler_id);
  g_signal_handler_disconnect (widget->shell_introspect,
                               widget->windows_changed_handler_id);
  widget->windows_changed_handler_id = 0;
  shell_introspect_unref_listeners (widget->shell_introspect);
}

static void
on_stack_switch (GtkStack *stack,
                 GParamSpec *pspec,
                 gpointer *data)
{
  ScreenCastWidget *widget = (ScreenCastWidget *)data;
  GtkWidget *visible_child;

  gtk_container_foreach (GTK_CONTAINER (widget->monitor_list),
                         unselect_row_cb,
                         widget->monitor_list);
  gtk_container_foreach (GTK_CONTAINER (widget->window_list),
                         unselect_row_cb,
                         widget->window_list);

  visible_child = gtk_stack_get_visible_child (stack);
  if (visible_child == widget->window_selection)
    {
      if (!widget->windows_changed_handler_id)
        connect_windows_changed_listener (widget);
    }
  else
    {
      if (widget->windows_changed_handler_id)
        disconnect_windows_changed_listener (widget);
    }
}

static void
on_row_activated (GtkListBox *box,
                  GtkListBoxRow *row,
                  gpointer *data)
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
update_selected_row_cb (GtkWidget *widget,
                        gpointer user_data)
{
  GtkListBoxRow *row = GTK_LIST_BOX_ROW (widget);

  set_row_is_selected (row, gtk_list_box_row_is_selected (row));
}

static gboolean
emit_selection_change_in_idle_cb (gpointer data)
{
  ScreenCastWidget *widget = (ScreenCastWidget *)data;
  GList *selected_monitor_rows;
  GList *selected_window_rows;

  /* Update the selected rows */
  gtk_container_foreach (GTK_CONTAINER (widget->monitor_list),
                         update_selected_row_cb,
                         NULL);
  gtk_container_foreach (GTK_CONTAINER (widget->window_list),
                         update_selected_row_cb,
                         NULL);

  selected_monitor_rows = gtk_list_box_get_selected_rows (GTK_LIST_BOX (widget->monitor_list));
  selected_window_rows = gtk_list_box_get_selected_rows (GTK_LIST_BOX (widget->window_list));
  g_signal_emit (widget, signals[HAS_SELECTION_CHANGED], 0,
                 !!selected_monitor_rows || !!selected_window_rows);
  g_list_free (selected_monitor_rows);
  g_list_free (selected_window_rows);

  widget->selection_changed_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static void
schedule_selection_change (ScreenCastWidget *widget)
{
  if (widget->selection_changed_timeout_id > 0)
    return;

  widget->selection_changed_timeout_id =
    g_idle_add (emit_selection_change_in_idle_cb, widget);
}

static void
on_selected_rows_changed (GtkListBox *box,
                          ScreenCastWidget *widget)
{
  /* GtkListBox activates rows after selecting them, which prevents
   * us from emitting the HAS_SELECTION_CHANGED signal here */
  schedule_selection_change (widget);
}

static void
update_list_box_header (GtkListBoxRow *row,
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
  GList *selected_monitor_rows;
  GList *selected_window_rows;
  GList *l;

  selected_monitor_rows =
    gtk_list_box_get_selected_rows (GTK_LIST_BOX (widget->monitor_list));
  selected_window_rows =
    gtk_list_box_get_selected_rows (GTK_LIST_BOX (widget->window_list));
  if (!selected_monitor_rows && !selected_window_rows)
    return FALSE;

  for (l = selected_monitor_rows; l; l = l->next)
    {
      GtkWidget *monitor_widget = gtk_bin_get_child (l->data);
      Monitor *monitor;

      monitor = g_object_get_qdata (G_OBJECT (monitor_widget),
                                    quark_monitor_widget_data);

      g_variant_builder_add (source_selections_builder, "(us)",
                             SCREEN_CAST_SOURCE_TYPE_MONITOR,
                             monitor_get_connector (monitor));
    }
  g_list_free (selected_monitor_rows);
  for (l = selected_window_rows; l; l = l->next)
    {
      GtkWidget *window_widget = gtk_bin_get_child (l->data);
      Window *window;

      window = g_object_get_qdata (G_OBJECT (window_widget),
                                    quark_window_widget_data);

      g_variant_builder_add (source_selections_builder, "(ut)",
                             SCREEN_CAST_SOURCE_TYPE_WINDOW,
                             window_get_id (window));
    }
  g_list_free (selected_window_rows);

  return TRUE;
}

void
screen_cast_widget_add_selections (ScreenCastWidget *widget,
                                   GVariantBuilder *selections_builder)
{
  GVariantBuilder source_selections_builder;

  g_variant_builder_init (&source_selections_builder, G_VARIANT_TYPE ("a(u?)"));
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
  g_autofree char *monitor_heading = NULL;
  g_autofree char *window_heading = NULL;

  if (app_id && strcmp (app_id, "") != 0)
    {
      g_autofree char *id = NULL;
      g_autoptr(GAppInfo) info = NULL;
      const gchar *display_name = NULL;

      id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (id));
      if (info)
        display_name = g_app_info_get_display_name (info);
      else
        display_name = app_id;
      monitor_heading = g_strdup_printf (_("Select monitor to share with %s"),
                                         display_name);
      window_heading = g_strdup_printf (_("Select window to share with %s"),
                                        display_name);
    }
  else
    {
      monitor_heading = g_strdup (_("Select monitor to share with the requesting application"));
      window_heading = g_strdup (_("Select window to share with the requesting application"));
    }

  gtk_label_set_label (GTK_LABEL (widget->monitor_heading), monitor_heading);
  gtk_label_set_label (GTK_LABEL (widget->window_heading), window_heading);
}

void
screen_cast_widget_set_allow_multiple (ScreenCastWidget *widget,
                                       gboolean multiple)
{
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (widget->monitor_list),
                                   multiple ? GTK_SELECTION_MULTIPLE
                                            : GTK_SELECTION_SINGLE);
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (widget->window_list),
                                   multiple ? GTK_SELECTION_MULTIPLE
                                            : GTK_SELECTION_SINGLE);
}

void
screen_cast_widget_set_source_types (ScreenCastWidget *screen_cast_widget,
                                     ScreenCastSourceType source_types)
{
  if (source_types & SCREEN_CAST_SOURCE_TYPE_MONITOR)
    gtk_widget_show (screen_cast_widget->monitor_selection);

  if (source_types & SCREEN_CAST_SOURCE_TYPE_WINDOW)
    gtk_widget_show (screen_cast_widget->window_selection);

  if (__builtin_popcount (source_types) > 1)
    gtk_widget_show (screen_cast_widget->source_type_switcher);
}

static void
screen_cast_widget_finalize (GObject *object)
{
  ScreenCastWidget *widget = SCREEN_CAST_WIDGET (object);

  g_signal_handler_disconnect (widget->display_state_tracker,
                               widget->monitors_changed_handler_id);

  if (widget->windows_changed_handler_id)
    disconnect_windows_changed_listener (widget);

  if (widget->selection_changed_timeout_id > 0)
    {
      g_source_remove (widget->selection_changed_timeout_id);
      widget->selection_changed_timeout_id = 0;
    }

  G_OBJECT_CLASS (screen_cast_widget_parent_class)->finalize (object);
}

static void
screen_cast_widget_init (ScreenCastWidget *widget)
{
  GtkScrolledWindow *scrolled_window;
  GtkAdjustment *vadjustment;

  gtk_widget_init_template (GTK_WIDGET (widget));

  screen_cast_widget_set_app_id (widget, NULL);
  screen_cast_widget_set_allow_multiple (widget, FALSE);

  gtk_list_box_set_header_func (GTK_LIST_BOX (widget->monitor_list),
                                update_list_box_header,
                                NULL, NULL);
  gtk_list_box_set_header_func (GTK_LIST_BOX (widget->window_list),
                                update_list_box_header,
                                NULL, NULL);
  scrolled_window = GTK_SCROLLED_WINDOW (widget->window_list_scrolled);
  vadjustment = gtk_scrolled_window_get_vadjustment (scrolled_window);
  gtk_list_box_set_adjustment (GTK_LIST_BOX (widget->window_list), vadjustment);

  g_signal_connect (widget->source_type, "notify::visible-child",
                    G_CALLBACK (on_stack_switch),
                    widget);
  g_signal_connect (widget->monitor_list, "row-activated",
                    G_CALLBACK (on_row_activated),
                    NULL);
  g_signal_connect (widget->window_list, "row-activated",
                    G_CALLBACK (on_row_activated),
                    NULL);
  g_signal_connect (widget->monitor_list, "selected-rows-changed",
                    G_CALLBACK (on_selected_rows_changed),
                    widget);
  g_signal_connect (widget->window_list, "selected-rows-changed",
                    G_CALLBACK (on_selected_rows_changed),
                    widget);

  widget->display_state_tracker = display_state_tracker_get ();
  widget->monitors_changed_handler_id =
    g_signal_connect (widget->display_state_tracker,
                      "monitors-changed",
                      G_CALLBACK (on_monitors_changed),
                      widget);
  widget->shell_introspect = shell_introspect_get ();

  update_monitors_list (widget);
  update_windows_list (widget);

  gtk_widget_show (widget->monitor_list);
  gtk_widget_show (widget->window_list);
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
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, source_type_switcher);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, source_type);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, monitor_selection);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, window_selection);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, monitor_heading);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, monitor_list);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, window_heading);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, window_list);
  gtk_widget_class_bind_template_child (widget_class, ScreenCastWidget, window_list_scrolled);

  quark_monitor_widget_data = g_quark_from_static_string ("-monitor-widget-connector-quark");
  quark_window_widget_data = g_quark_from_static_string ("-window-widget-connector-quark");
}

void
init_screen_cast_widget (void)
{
  g_type_ensure (screen_cast_widget_get_type ());
}
