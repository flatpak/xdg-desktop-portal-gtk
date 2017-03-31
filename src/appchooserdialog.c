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
  GtkWidget *list;
  GtkWidget *stack;
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
row_activated (GtkListBox *list,
               GtkWidget *row,
               AppChooserDialog *dialog)
{
  GAppInfo *info = NULL;

  gtk_widget_hide (GTK_WIDGET (dialog));
  info = app_chooser_row_get_info (APP_CHOOSER_ROW (row));
  g_signal_emit (dialog, signals[DONE], 0, info);
}

static void
button_clicked (GtkWidget *button,
                AppChooserDialog *dialog)
{
  gtk_widget_hide (GTK_WIDGET (dialog));
  g_signal_emit (dialog, signals[DONE], 0, NULL);
}

static void
app_chooser_dialog_class_init (AppChooserDialogClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

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
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, list);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, stack);
  gtk_widget_class_bind_template_callback (widget_class, row_activated);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
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
  GtkWidget *default_row;

  if (provider == NULL)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_path (provider, "resource:///org/freedesktop/portal/desktop/gtk/appchooserdialog.css", NULL);
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                 GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  dialog = g_object_new (app_chooser_dialog_get_type (), NULL);

  gtk_button_set_label (GTK_BUTTON (dialog->cancel_button), cancel_label);
  gtk_header_bar_set_title (GTK_HEADER_BAR (dialog->titlebar), title);

  default_row = NULL;

  n_choices = g_strv_length ((char **)choices);
  for (i = 0; i < n_choices; i++)
    {
      g_autofree char *desktop_id = g_strconcat (choices[i], ".desktop", NULL);
      g_autoptr(GAppInfo) info = G_APP_INFO (g_desktop_app_info_new (desktop_id));
      GtkWidget *row;

      row = GTK_WIDGET (app_chooser_row_new (info));
      gtk_widget_set_visible (row, TRUE);
      gtk_flow_box_insert (GTK_FLOW_BOX (dialog->list), row, -1);

      if (g_strcmp0 (choices[i], default_id) == 0)
        default_row = row;
    }

  if (default_row)
    gtk_widget_grab_focus (default_row);

  gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), n_choices > 0 ? "list" : "empty");

  return dialog;
}
