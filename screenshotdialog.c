#define _GNU_SOURCE 1

#include <string.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <glib/gi18n.h>

#include "screenshotdialog.h"

struct _ScreenshotDialog {
  GtkWindow parent;

  GtkWidget *image;
  GtkWidget *heading;
  GtkWidget *accept_button;
};

struct _ScreenshotDialogClass {
  GtkWindowClass parent_class;
};

enum {
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (ScreenshotDialog, screenshot_dialog, GTK_TYPE_WINDOW)

static void
screenshot_dialog_init (ScreenshotDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
button_clicked (GtkWidget *button,
                ScreenshotDialog *dialog)
{
  int response;

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (button == dialog->accept_button)
    response = GTK_RESPONSE_OK;
  else
    response = GTK_RESPONSE_CANCEL;

  g_signal_emit (dialog, signals[DONE], 0, response);
}

static void
screenshot_dialog_class_init (ScreenshotDialogClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1,
                                G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/screenshotdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, accept_button);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, image);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
}

ScreenshotDialog *
screenshot_dialog_new (const char *app_id,
                       const char *filename)
{
  ScreenshotDialog *dialog;
  g_autofree char *heading = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  g_autoptr(GError) error = NULL;

  dialog = g_object_new (screenshot_dialog_get_type (), NULL);

  if (strcmp (app_id, "") != 0)
    {
      g_autofree char *id = NULL;
      g_autoptr(GAppInfo) info = NULL;
      id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (id));
      heading = g_strdup_printf (_("Share this screenshot with %s ?"), g_app_info_get_display_name (info));
    }
  else
    heading = g_strdup (_("Share this screenshot with the requesting application ?"));

  gtk_label_set_label (GTK_LABEL (dialog->heading), heading);

  pixbuf = gdk_pixbuf_new_from_file_at_scale (filename, 500, 400, TRUE, &error);
  if (error)
    g_print ("%s\n", error->message);

  gtk_image_set_from_pixbuf (GTK_IMAGE (dialog->image), pixbuf);

  return dialog;
}
