#include <gtk/gtk.h>

#define EMAILTB_TYPE_DIALOG (email_tb_dialog_get_type ())
#define EMAILTB_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, EMAILTB_TYPE_DIALOG, EmailTbDialog))

typedef struct _EmailTbDialog EmailTbDialog;
typedef struct _EmailTbDialogClass EmailTbDialogClass;

EmailTbDialog * email_tb_dialog_new (const char **attachments_list_text,
                                     const char **attachments_list_link);

