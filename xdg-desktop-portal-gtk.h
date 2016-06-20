#include <gio/gio.h>

typedef struct {
  char *id;
  char *app_id;
  char *sender;
  GDBusInterfaceSkeleton *skeleton;
} DialogHandle;

void dialog_handle_register (DialogHandle *handle);
void dialog_handle_unregister (DialogHandle *handle);
DialogHandle *dialog_handle_find (const char *arg_sender,
                                  const char *arg_app_id,
                                  const char *arg_handle,
                                  GType skel_type);
