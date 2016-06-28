#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE (ScreenshotDialog, screenshot_dialog, SCREENSHOT, DIALOG, GtkWindow)

ScreenshotDialog * screenshot_dialog_new (const char *app_id,
                                          const char *filename);
