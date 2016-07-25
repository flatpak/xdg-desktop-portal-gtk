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

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "appchooserdialog.h"
#include "appchooserrow.h"

struct _AppChooserDialog {
  GtkWindow parent;

  GtkWidget *scrolled_window;
  GtkWidget *titlebar;
  GtkWidget *cancel_button;
  GtkWidget *search_button;
  GtkWidget *accept_button;
  GtkWidget *heading;
  GtkWidget *stack;
  GtkWidget *search_entry;
  GtkWidget *list;
  GtkWidget *more;

  GtkWidget *selected;
};

struct _AppChooserDialogClass {
  GtkWindowClass parent_class;
};

enum {
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (AppChooserDialog, app_chooser_dialog, GTK_TYPE_WINDOW)

static void
app_chooser_dialog_init (AppChooserDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
show_row (GtkWidget *row, gpointer data)
{
  gtk_widget_show (row);
}

static void
show_all (AppChooserDialog *dialog)
{
  if (dialog->more)
    {
      g_object_set (dialog->scrolled_window,
                    "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                    NULL);
      gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
      gtk_container_remove (GTK_CONTAINER (dialog->list), dialog->more);
      dialog->more = NULL;
      gtk_container_forall (GTK_CONTAINER (dialog->list), show_row, dialog);
    }
}

static void
row_activated (GtkListBox *list,
               GtkWidget *row,
               AppChooserDialog *dialog)
{
  if (row == dialog->more)
    {
      show_all (dialog);
    }
  else
    {
      if (dialog->selected)
        app_chooser_row_set_selected (APP_CHOOSER_ROW (dialog->selected), FALSE);
      dialog->selected = row;
      if (dialog->selected)
        app_chooser_row_set_selected (APP_CHOOSER_ROW (dialog->selected), TRUE);
      gtk_widget_set_sensitive (dialog->accept_button, dialog->selected != NULL);
    }
}

static void
button_clicked (GtkWidget *button,
                AppChooserDialog *dialog)
{
  GAppInfo *info = NULL;

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (button == dialog->accept_button && dialog->selected)
    info = app_chooser_row_get_info (APP_CHOOSER_ROW (dialog->selected));

  g_signal_emit (dialog, signals[DONE], 0, info);
}

static gboolean
filter_func (GtkListBoxRow *row,
             gpointer data)
{
  AppChooserDialog *dialog = data;
  const char *text;
  GAppInfo *info;

  text = gtk_entry_get_text (GTK_ENTRY (dialog->search_entry));
  info = app_chooser_row_get_info (APP_CHOOSER_ROW (row));

  return strcasestr (g_app_info_get_display_name (info), text) != NULL ||
         strcasestr (g_app_info_get_name (info), text) != NULL ||
         strcasestr (g_app_info_get_executable (info), text) != NULL;
}

static void
search_changed (GtkSearchEntry *entry,
                AppChooserDialog *dialog)
{
  gtk_list_box_invalidate_filter (GTK_LIST_BOX (dialog->list));
}

static gboolean
app_chooser_dialog_key_press (GtkWidget *widget,
                              GdkEventKey *event)
{
  AppChooserDialog *dialog = APP_CHOOSER_DIALOG (widget);

  if (gtk_search_entry_handle_event (GTK_SEARCH_ENTRY (dialog->search_entry), (GdkEvent *)event))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->search_button), TRUE);
      return TRUE;
    }

 if (GTK_WIDGET_CLASS (app_chooser_dialog_parent_class)->key_press_event (widget, event))
   return TRUE;

  return FALSE;
}

static void
stop_search (GtkSearchEntry *entry,
             AppChooserDialog *dialog)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->search_button), FALSE);
}

static void
search_toggled (GtkToggleButton *button,
                AppChooserDialog *dialog)
{
  gboolean active = gtk_toggle_button_get_active (button);

  if (active)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "search");
      show_all (dialog);
      gtk_list_box_set_filter_func (GTK_LIST_BOX (dialog->list),
                                    filter_func, dialog, NULL);
      gtk_entry_grab_focus_without_selecting (GTK_ENTRY (dialog->search_entry));
    }
  else
    {
      gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "heading");
      gtk_list_box_set_filter_func (GTK_LIST_BOX (dialog->list),
                                    NULL, NULL, NULL);
      gtk_entry_set_text (GTK_ENTRY (dialog->search_entry), "");
    }
}

static void
app_chooser_dialog_class_init (AppChooserDialogClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  widget_class->key_press_event = app_chooser_dialog_key_press;

  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1,
                                G_TYPE_APP_INFO);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/appchooserdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, titlebar);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, search_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, accept_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, stack);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, search_entry);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, list);
  gtk_widget_class_bind_template_callback (widget_class, row_activated);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, search_toggled);
  gtk_widget_class_bind_template_callback (widget_class, search_changed);
  gtk_widget_class_bind_template_callback (widget_class, stop_search);
}

AppChooserDialog *
app_chooser_dialog_new (const char **choices,
                        const char *default_id,
                        const char *cancel_label,
                        const char *accept_label,
                        const char *title,
                        const char *heading)
{
  AppChooserDialog *dialog;
  int n_choices;
  int i;
  static GtkCssProvider *provider;

  if (provider == NULL)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (provider, "/org/freedesktop/portal/desktop/gtk/appchooserdialog.css");
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                 GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  dialog = g_object_new (app_chooser_dialog_get_type (), NULL);

  gtk_button_set_label (GTK_BUTTON (dialog->cancel_button), cancel_label);
  gtk_button_set_label (GTK_BUTTON (dialog->accept_button), accept_label);
  gtk_header_bar_set_title (GTK_HEADER_BAR (dialog->titlebar), title);
  gtk_label_set_label (GTK_LABEL (dialog->heading), heading);

  n_choices = g_strv_length ((char **)choices);
  for (i = 0; i < n_choices; i++)
    {
      g_autofree char *desktop_id = g_strconcat (choices[i], ".desktop", NULL);
      g_autoptr(GAppInfo) info = G_APP_INFO (g_desktop_app_info_new (desktop_id));
      GtkWidget *row;

      row = GTK_WIDGET (app_chooser_row_new (info));
      gtk_widget_set_visible (row, i < 3 || n_choices < 5);
      gtk_list_box_insert (GTK_LIST_BOX (dialog->list), row, -1);
    }

  if (default_id != NULL)
    app_chooser_dialog_set_selected (dialog, default_id);

  if (n_choices > 4)
    {
      GtkWidget *row;
      GtkWidget *icon;
      row = gtk_list_box_row_new ();
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
      icon = gtk_image_new_from_icon_name ("view-more-symbolic", GTK_ICON_SIZE_BUTTON);
      g_object_set (icon, "margin", 10, NULL);
      gtk_widget_set_halign (icon, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);
      gtk_container_add (GTK_CONTAINER (row), icon);
      gtk_widget_show_all (row);

      dialog->more = row;
      gtk_list_box_insert (GTK_LIST_BOX (dialog->list), row, -1);
    }

  return dialog;
}

void
app_chooser_dialog_set_selected (AppChooserDialog *dialog,
                                 const char *choice_id)
{
  AppChooserRow *row = NULL;
  GAppInfo *info = NULL;
  g_autoptr(GList) choices = NULL;
  GList *l = NULL;
  g_autofree char *desktop_id = g_strconcat (choice_id, ".desktop", NULL);

  choices = gtk_container_get_children (GTK_CONTAINER (dialog->list));
  for (l = choices; l != NULL; l = g_list_next (l))
    {
      row = APP_CHOOSER_ROW (l->data);
      info = app_chooser_row_get_info (row);

      if (g_strcmp0 (desktop_id, g_app_info_get_id (info)) == 0)
        {
          g_signal_emit_by_name (GTK_LIST_BOX (dialog->list), "row-activated", row, dialog);
          break;
        }
    }
}
