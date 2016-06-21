#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE (AppChooserDialog, app_chooser_dialog, APP, CHOOSER_DIALOG, GtkWindow)

AppChooserDialog * app_chooser_dialog_new (const char **app_ids,
                                           const char *cancel_label,
                                           const char *accept_label,
                                           const char *title,
                                           const char *heading);
const char *app_chooser_dialog_get_selected (AppChooserDialog *dialog);
