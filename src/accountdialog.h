#include <gtk/gtk.h>

G_DECLARE_FINAL_TYPE (AccountDialog, account_dialog, ACCOUNT, DIALOG, GtkWindow)

AccountDialog * account_dialog_new (const char *app_id,
                                    const char *user_name,
                                    const char *real_name,
                                    const char *icon_file,
                                    const char *reason);

const char *account_dialog_get_user_name (AccountDialog *dialog);
const char *account_dialog_get_real_name (AccountDialog *dialog);
const char *account_dialog_get_icon_file (AccountDialog *dialog);
