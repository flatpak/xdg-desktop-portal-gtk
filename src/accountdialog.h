#include <gtk/gtk.h>

#define ACCOUNT_TYPE_DIALOG (account_dialog_get_type ())
#define ACCOUNT_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, ACCOUNT_TYPE_DIALOG, AccountDialog))

typedef struct _AccountDialog AccountDialog;
typedef struct _AccountDialogClass AccountDialogClass;

AccountDialog * account_dialog_new (const char *app_id,
                                    const char *user_name,
                                    const char *real_name,
                                    const char *icon_file,
                                    const char *reason);

const char *account_dialog_get_user_name (AccountDialog *dialog);
const char *account_dialog_get_real_name (AccountDialog *dialog);
const char *account_dialog_get_icon_file (AccountDialog *dialog);
