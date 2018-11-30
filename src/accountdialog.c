#define _GNU_SOURCE 1

#include <string.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <glib/gi18n.h>

#include "accountdialog.h"

struct _AccountDialog {
  GtkWindow parent;

  GtkWidget *heading;
  GtkWidget *reason;
  GtkWidget *accept_button;
  GtkWidget *name;
  GtkWidget *name_readonly;
  GtkWidget *name_stack;
  GtkWidget *fullname;
  GtkWidget *fullname_readonly;
  GtkWidget *fullname_stack;
  GtkWidget *image;
  GtkWidget *image_readonly;
  GtkWidget *image_stack;

  char *icon_file;
};

struct _AccountDialogClass {
  GtkWindowClass parent_class;
};

enum {
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (AccountDialog, account_dialog, GTK_TYPE_WINDOW)

static void
account_dialog_init (AccountDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
account_dialog_finalize (GObject *object)
{
  AccountDialog *dialog = ACCOUNT_DIALOG (object);

  g_clear_pointer (&dialog->icon_file, g_free);

  G_OBJECT_CLASS (account_dialog_parent_class)->finalize (object);
}

static gboolean
account_dialog_delete_event (GtkWidget *dialog, GdkEventAny *event)
{
  gtk_widget_hide (dialog);

  g_signal_emit (dialog, signals[DONE], 0, GTK_RESPONSE_CANCEL, NULL);

  return TRUE;
}

static void
button_clicked (GtkWidget *button,
                AccountDialog *dialog)
{
  int response;
  const char *user_name;
  const char *real_name;

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (button == dialog->accept_button)
    response = GTK_RESPONSE_OK;
  else
    response = GTK_RESPONSE_CANCEL;

  user_name = gtk_entry_get_text (GTK_ENTRY (dialog->name));
  real_name = gtk_entry_get_text (GTK_ENTRY (dialog->fullname));
  g_signal_emit (dialog, signals[DONE], 0, response, user_name, real_name, dialog->icon_file);
}

static void
dialog_set_icon_file (AccountDialog *dialog,
                      const char    *icon_file)
{
  g_clear_pointer (&dialog->icon_file, g_free);
  dialog->icon_file = g_strdup (icon_file);

  if (icon_file)
    {
      g_autoptr(GdkPixbuf) pixbuf = NULL;
      g_autoptr(GError) error = NULL;

      pixbuf = gdk_pixbuf_new_from_file_at_scale (icon_file, 64, 64, TRUE, &error);
      if (error)
        g_warning ("Failed to load account %s: %s", icon_file, error->message);
      gtk_image_set_from_pixbuf (GTK_IMAGE (dialog->image), pixbuf);
      gtk_style_context_remove_class (gtk_widget_get_style_context (dialog->image), "dim-label");
    }
  else
    {
      gtk_image_set_from_icon_name (GTK_IMAGE (dialog->image), "camera-photo-symbolic", 1);
      gtk_image_set_pixel_size (GTK_IMAGE (dialog->image), 64);
      gtk_style_context_add_class (gtk_widget_get_style_context (dialog->image), "dim-label");
    }
}

static void
file_chooser_response (GtkWidget *widget,
                       int response,
                       gpointer user_data)
{
  AccountDialog *dialog = user_data;
  g_autofree char *file = NULL;

  file = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));

  gtk_widget_destroy (widget);

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CANCEL:
      break;

    case GTK_RESPONSE_OK:
      dialog_set_icon_file (dialog, file);
      break;

    case GTK_RESPONSE_CLOSE:
      dialog_set_icon_file (dialog, NULL);
      break;
    }
}

static void
update_preview_cb (GtkFileChooser *file_chooser, gpointer data)
{
  GtkWidget *preview;
  char *filename;
  GdkPixbuf *pixbuf;
  gboolean have_preview;

  preview = GTK_WIDGET (data);
  filename = gtk_file_chooser_get_preview_filename (file_chooser);

  pixbuf = gdk_pixbuf_new_from_file_at_size (filename, 128, 128, NULL);
  have_preview = (pixbuf != NULL);
  g_free (filename);

  gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
  if (pixbuf)
    g_object_unref (pixbuf);

  gtk_file_chooser_set_preview_widget_active (file_chooser, have_preview);
}

static void
image_button_clicked (AccountDialog *dialog)
{
  GtkWidget *chooser;
  GtkWidget *preview;
  GtkFileFilter *filter;

  chooser = gtk_file_chooser_dialog_new (_("Select an Image"),
                                         GTK_WINDOW (dialog),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         _("Cancel"), GTK_RESPONSE_CANCEL,
                                         _("Select"), GTK_RESPONSE_OK,
                                         _("Clear"),  GTK_RESPONSE_CLOSE,
                                         NULL);
  gtk_window_set_modal (GTK_WINDOW (chooser), TRUE);

  gtk_dialog_set_default_response (GTK_DIALOG (chooser), GTK_RESPONSE_OK);

  preview = gtk_image_new ();
  g_object_set (preview, "margin", 10, NULL);
  gtk_widget_show (preview);
  gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (chooser), preview);
  gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (chooser), FALSE);
  g_signal_connect (chooser, "update-preview", G_CALLBACK (update_preview_cb), preview);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Images"));
  gtk_file_filter_add_pixbuf_formats (filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  g_signal_connect (chooser, "response", G_CALLBACK (file_chooser_response), dialog);

  gtk_widget_show (chooser);
}

static void
override_button_clicked (GtkButton *button,
                         AccountDialog *dialog)
{
   gtk_stack_set_visible_child_name (GTK_STACK (dialog->name_stack), "edit");
   gtk_stack_set_visible_child_name (GTK_STACK (dialog->fullname_stack), "edit");
   gtk_stack_set_visible_child_name (GTK_STACK (dialog->image_stack), "edit");
   gtk_widget_set_opacity (GTK_WIDGET (button), 0);
}

static void
account_dialog_class_init (AccountDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  object_class->finalize = account_dialog_finalize;

  widget_class->delete_event = account_dialog_delete_event;

  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 4,
                                G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/accountdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, accept_button);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, reason);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, name);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, name_readonly);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, name_stack);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, fullname);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, fullname_readonly);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, fullname_stack);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, image);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, image_readonly);
  gtk_widget_class_bind_template_child (widget_class, AccountDialog, image_stack);

  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, image_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, override_button_clicked);
}

AccountDialog *
account_dialog_new (const char *app_id,
                    const char *user_name,
                    const char *real_name,
                    const char *icon_file,
                    const char *reason)
{
  AccountDialog *dialog;
  g_autofree char *heading = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  g_autoptr(GError) error = NULL;

  dialog = g_object_new (account_dialog_get_type (), NULL);

  if (strcmp (app_id, "") != 0)
    {
      g_autofree char *id = NULL;
      g_autoptr(GAppInfo) info = NULL;
      id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (id));
      heading = g_strdup_printf (_("Share your personal information with %s?"), g_app_info_get_display_name (info));
    }
  else
    heading = g_strdup (_("Share your personal information with the requesting application?"));

  gtk_label_set_label (GTK_LABEL (dialog->heading), heading);
  gtk_entry_set_text (GTK_ENTRY (dialog->name), user_name);
  gtk_label_set_label (GTK_LABEL (dialog->name_readonly), user_name);
  gtk_entry_set_text (GTK_ENTRY (dialog->fullname), real_name);
  gtk_label_set_label (GTK_LABEL (dialog->fullname_readonly), real_name);

  if (reason)
    gtk_label_set_label (GTK_LABEL (dialog->reason), reason);
  else
    gtk_widget_hide (dialog->reason);

  pixbuf = gdk_pixbuf_new_from_file_at_scale (icon_file, 64, 64, TRUE, &error);
  if (error)
    g_warning ("Failed to load account %s: %s", icon_file, error->message);
  else
    {
      gtk_image_set_from_pixbuf (GTK_IMAGE (dialog->image), pixbuf);
      gtk_image_set_from_pixbuf (GTK_IMAGE (dialog->image_readonly), pixbuf);
    }

  dialog->icon_file = g_strdup (icon_file);

  return dialog;
}
