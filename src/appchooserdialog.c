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
#include <glib/gi18n.h>
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
  GtkWidget *heading;

  char *content_type;
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
app_chooser_dialog_finalize (GObject *object)
{
  AppChooserDialog *dialog = APP_CHOOSER_DIALOG (object);

  g_free (dialog->content_type);

  G_OBJECT_CLASS (app_chooser_dialog_parent_class)->finalize (object);
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
show_error_dialog (const gchar *primary,
                   const gchar *secondary,
                   GtkWindow *parent)
{
  GtkWidget *message_dialog;

  message_dialog = gtk_message_dialog_new (parent, 0,
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_OK,
                                           NULL);
  g_object_set (message_dialog,
                "text", primary,
                "secondary-text", secondary,
                NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (message_dialog), GTK_RESPONSE_OK);
  gtk_widget_show (message_dialog);
  g_signal_connect (message_dialog, "response",
                    G_CALLBACK (gtk_widget_destroy), NULL);
}

static void
link_activated (GtkWidget *label,
                const char *uri,
                AppChooserDialog *dialog)
{
  g_autofree char *option = NULL;
  g_autoptr(GSubprocess) process = NULL;
  g_autoptr(GError) error = NULL;

  if (dialog->content_type)
    option = g_strconcat ("--search=", dialog->content_type, NULL);
  else
    option = g_strdup ("--mode=overview");

  process = g_subprocess_new (0, &error, "gnome-software", option, NULL);
  if (!process)
    show_error_dialog (_("Failed to start Software"), error->message, GTK_WINDOW (dialog));
}

static void
app_chooser_dialog_class_init (AppChooserDialogClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = app_chooser_dialog_finalize;

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
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, heading);
  gtk_widget_class_bind_template_callback (widget_class, row_activated);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, link_activated);
}

AppChooserDialog *
app_chooser_dialog_new (const char **choices,
                        const char *default_id,
                        const char *content_type,
                        const char *filename)
{
  AppChooserDialog *dialog;
  int n_choices;
  int i;
  static GtkCssProvider *provider;
  GtkWidget *default_row;

  if (provider == NULL)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (provider, "/org/freedesktop/portal/desktop/gtk/appchooserdialog.css");
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                 GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  dialog = g_object_new (app_chooser_dialog_get_type (), NULL);

  dialog->content_type = g_strdup (content_type);

  if (filename)
    {
      g_autofree char *heading = NULL;

      heading = g_strdup_printf (_("Select an application to open '%s'. More applications are available in <a href='software'>Software.</a>"), filename);
      gtk_label_set_label (GTK_LABEL (dialog->heading), heading);
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (dialog->heading), _("Select an application. More applications are available in <a href='software'>Software.</a>"));
    }

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
