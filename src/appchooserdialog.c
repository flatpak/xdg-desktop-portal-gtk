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

#define LOCATION_MAX_LENGTH 40

struct _AppChooserDialog {
  GtkWindow parent;

  GtkWidget *scrolled_window;
  GtkWidget *titlebar;
  GtkWidget *cancel_button;
  GtkWidget *search_button;
  GtkWidget *more_button;
  GtkWidget *list;
  GtkWidget *full_list;
  GtkWidget *full_list_box;
  GtkWidget *heading;
  GtkWidget *search_bar;
  GtkWidget *search_entry;
  GtkWidget *stack;
  GtkWidget *separator;
  GtkWidget *empty_label;

  char *content_type;
  char *search_text;

  char **choices;

  GAppInfo *info;
};

struct _AppChooserDialogClass {
  GtkWindowClass parent_class;

  void (* close) (AppChooserDialog *dialog);
};

enum {
  CLOSE,
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
  g_free (dialog->search_text);
  g_strfreev (dialog->choices);

  G_OBJECT_CLASS (app_chooser_dialog_parent_class)->finalize (object);
}

GAppInfo *
app_chooser_dialog_get_info (AppChooserDialog *dialog)
{
  return dialog->info;
}

static void
close_dialog (AppChooserDialog *dialog,
              GAppInfo *info)
{
  dialog->info = info;
  g_signal_emit (dialog, signals[CLOSE], 0);
}

static void
row_activated (GtkListBox *list,
               GtkWidget *row,
               AppChooserDialog *dialog)
{
  close_dialog (dialog, app_chooser_row_get_info (APP_CHOOSER_ROW (row)));
}

static void
cancel_clicked (GtkWidget *button,
                AppChooserDialog *dialog)
{
  close_dialog (dialog, NULL);
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
launch_software (AppChooserDialog *dialog)
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
link_activated (GtkWidget *label,
                const char *uri,
                AppChooserDialog *dialog)
{
  launch_software (dialog);
}

static void
find_in_software (GtkWidget *button,
                  AppChooserDialog *dialog)
{
  launch_software (dialog);
}

static void
populate_full_list (AppChooserDialog *dialog)
{
  GList *apps, *l;

  apps = g_app_info_get_all ();

  for (l = apps; l; l = l->next)
    {
      GAppInfo *info = l->data;
      GtkWidget *row;

      row = GTK_WIDGET (app_chooser_row_new (info));
      gtk_widget_set_visible (row, TRUE);
      gtk_flow_box_insert (GTK_FLOW_BOX (dialog->full_list), row, -1);
    }

  g_list_free_full (apps, g_object_unref);
}

static gboolean
scroll_down (gpointer data)
{
  AppChooserDialog *dialog = data;
  GtkAdjustment *adj;
  GtkAllocation alloc;

  gtk_widget_get_allocation (dialog->full_list, &alloc);
  adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (dialog->scrolled_window));
  gtk_adjustment_set_value (adj, alloc.y);

  return G_SOURCE_REMOVE;
}

static void
show_full_list (AppChooserDialog *dialog)
{
  if (!gtk_widget_get_visible (dialog->more_button))
    return;

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (dialog->scrolled_window),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);

  gtk_widget_hide (dialog->more_button);
  gtk_widget_show (dialog->search_button);
  gtk_widget_show (dialog->full_list_box);

  populate_full_list (dialog);

  g_idle_add (scroll_down, dialog);
}

static void
more_clicked (GtkButton *button,
              AppChooserDialog *dialog)
{
  show_full_list (dialog);
}

static void
more2_clicked (GtkButton *button,
               AppChooserDialog *dialog)
{
  gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "list");
  gtk_widget_hide (dialog->separator);
  show_full_list (dialog);
}

static gboolean
filter_func (GtkFlowBoxChild *child,
             gpointer data)
{
  AppChooserRow *row = APP_CHOOSER_ROW (child);
  AppChooserDialog *dialog = data;
  GAppInfo *info;
  char *name;
  gboolean match;

  if (!dialog->search_text)
    return TRUE;

  info = app_chooser_row_get_info (row);

  name = g_utf8_casefold (g_app_info_get_name (info), -1);
  match = g_str_has_prefix (name, dialog->search_text);
  g_free (name);

  return match;
}

static void
search_changed (GtkSearchEntry *entry,
                gpointer data)
{
  AppChooserDialog *dialog = data;

  g_free (dialog->search_text);
  dialog->search_text = g_utf8_casefold (gtk_entry_get_text (GTK_ENTRY (dialog->search_entry)), -1);

  gtk_flow_box_invalidate_filter (GTK_FLOW_BOX (dialog->full_list));
}

static gboolean
key_press_event_cb (GtkWidget *widget,
                    GdkEvent *event,
                    gpointer data)
{
  AppChooserDialog *dialog = data;

  if (gtk_search_bar_handle_event (GTK_SEARCH_BAR (dialog->search_bar), event) == GDK_EVENT_STOP)
    {
      show_full_list (dialog);
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
app_chooser_dialog_close (AppChooserDialog *dialog)
{
  gtk_window_close (GTK_WINDOW (dialog));
}

static void
app_chooser_dialog_class_init (AppChooserDialogClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkBindingSet *binding_set;

  object_class->finalize = app_chooser_dialog_finalize;

  class->close = app_chooser_dialog_close;

  signals[CLOSE] = g_signal_new ("close",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  binding_set = gtk_binding_set_by_class (class);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "close", 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/appchooserdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, titlebar);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, search_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, more_button);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, list);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, full_list_box);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, full_list);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, search_bar);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, search_entry);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, stack);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, separator);
  gtk_widget_class_bind_template_child (widget_class, AppChooserDialog, empty_label);
  gtk_widget_class_bind_template_callback (widget_class, row_activated);
  gtk_widget_class_bind_template_callback (widget_class, cancel_clicked);
  gtk_widget_class_bind_template_callback (widget_class, link_activated);
  gtk_widget_class_bind_template_callback (widget_class, more_clicked);
  gtk_widget_class_bind_template_callback (widget_class, more2_clicked);
  gtk_widget_class_bind_template_callback (widget_class, search_changed);
  gtk_widget_class_bind_template_callback (widget_class, key_press_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, find_in_software);
}

/* Ellipsize the location, keeping the suffix which is likely
 * to have more relevant information, such as filename and extension.
 */
static char *
shorten_location (const char *location)
{
  int len;

  len = g_utf8_strlen (location, -1);

  if (len < LOCATION_MAX_LENGTH)
    return g_strdup (location);

  for (; len >= 40; len--)
    location = g_utf8_next_char (location);

  return g_strconcat ("…", location, NULL);
}

AppChooserDialog *
app_chooser_dialog_new (const char **choices,
                        const char *default_id,
                        const char *content_type,
                        const char *location)
{
  AppChooserDialog *dialog;
  int n_choices;
  int i;
  static GtkCssProvider *provider;
  GtkWidget *default_row;
  g_autofree char *short_location = shorten_location (location);

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

  if (location)
    {
      g_autofree char *heading = NULL;

      heading = g_strdup_printf (_("Select an application to open “%s”. More applications are available in <a href='software'>Software.</a>"), short_location);
      gtk_label_set_label (GTK_LABEL (dialog->heading), heading);
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (dialog->heading), _("Select an application. More applications are available in <a href='software'>Software.</a>"));
    }

  default_row = NULL;

  dialog->choices = g_strdupv ((char **)choices);

  n_choices = g_strv_length ((char **)choices);
  if (n_choices == 0)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
      if (location)
        {
          g_autofree char *label = NULL;

          label = g_strdup_printf (_("Unable to find an application that is able to open “%s”."), short_location);
          gtk_label_set_label (GTK_LABEL (dialog->empty_label), label);
        }
      else
        {
          gtk_label_set_label (GTK_LABEL (dialog->empty_label), _("Unable to find a suitable application."));
        }
    }
  else
    {
      gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "list");
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

     if (n_choices < 4)
       gtk_widget_set_halign (dialog->list, GTK_ALIGN_START);

     if (default_row)
       gtk_widget_grab_focus (default_row);
    }

  gtk_flow_box_set_filter_func (GTK_FLOW_BOX (dialog->full_list), filter_func, dialog, NULL);

  return dialog;
}

void
app_chooser_dialog_update_choices (AppChooserDialog  *dialog,
                                   const char       **choices)
{
  int i;
  GPtrArray *new_choices;

  new_choices = g_ptr_array_new ();
  g_ptr_array_set_size (new_choices, g_strv_length (dialog->choices));
  for (i = 0; dialog->choices[i]; i++)
    new_choices->pdata[i] = dialog->choices[i];

  for (i = 0; choices[i]; i++)
    {
      g_autofree char *desktop_id = NULL;
      g_autoptr(GAppInfo) info = NULL;
      GtkWidget *row;

      if (g_strv_contains ((const char * const *)dialog->choices, choices[i]))
        continue;

      g_ptr_array_add (new_choices, g_strdup (choices[i]));

      desktop_id = g_strconcat (choices[i], ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (desktop_id));

      row = GTK_WIDGET (app_chooser_row_new (info));
      gtk_widget_set_visible (row, TRUE);
      gtk_flow_box_insert (GTK_FLOW_BOX (dialog->list), row, -1);
    }

  g_ptr_array_add (new_choices, NULL);

  g_free (dialog->choices);
  dialog->choices = (char **) g_ptr_array_free (new_choices, FALSE);
}
