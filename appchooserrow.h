#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE (AppChooserRow, app_chooser_row, APP, CHOOSER_ROW, GtkListBox)

AppChooserRow *app_chooser_row_new (GAppInfo *info);
void app_chooser_row_set_selected (AppChooserRow *row,
                                   gboolean selected);
GAppInfo *app_chooser_row_get_info (AppChooserRow *row);
