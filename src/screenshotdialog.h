#include <gtk/gtk.h>
#include "shell-dbus.h"

#define SCREENSHOT_TYPE_DIALOG (screenshot_dialog_get_type ())
#define SCREENSHOT_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, SCREENSHOT_TYPE_DIALOG, ScreenshotDialog))

typedef struct _ScreenshotDialog ScreenshotDialog;
typedef struct _ScreenshotDialogClass ScreenshotDialogClass;

ScreenshotDialog * screenshot_dialog_new (const char *app_id,
                                          gboolean interactive,
                                          OrgGnomeShellScreenshot *shell);
