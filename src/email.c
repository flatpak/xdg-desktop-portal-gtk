#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "email.h"
#include "request.h"
#include "utils.h"

static gboolean
compose_mail_evolution (const char  *address,
                        const char  *subject,
                        const char  *body,
                        const char **attachments,
                        GError     **error)
{
  g_autofree char *enc_subject = NULL;
  g_autofree char *enc_body = NULL;
  g_autoptr(GString) url = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  const char *argv[4];
  int i;
  g_autofree char *command = NULL;

  enc_subject = g_uri_escape_string (subject ? subject : "", NULL, FALSE);
  enc_body = g_uri_escape_string (body ? body : "", NULL, FALSE);

  url = g_string_new ("mailto:");

  g_string_append_printf (url, "\"%s\"", address ? address : "");
  g_string_append_printf (url, "?subject=%s", enc_subject);
  g_string_append_printf (url, "&body=%s", enc_body);

  for (i = 0; attachments[i]; i++)
    {
      g_autofree char *path = g_uri_escape_string (attachments[i], NULL, FALSE);
      g_string_append_printf (url, "&attach=%s", path);
    }

  argv[0] = "evolution";
  argv[1] = "--component=mail";
  argv[2] = url->str;
  argv[3] = NULL;

  command = g_strjoinv (" ", (char **)argv);
  g_debug ("Running: %s\n", command);

  subprocess = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_NONE, error);

  return subprocess != NULL;
}

static gboolean
compose_mail_thunderbird (const char  *address,
                          const char  *subject,
                          const char  *body,
                          const char **attachments,
                          GError     **error)
{
  g_autofree char *enc_subject = NULL;
  g_autofree char *enc_body = NULL;
  g_autoptr(GString) url = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  const char *argv[4];
  int i;
  g_autofree char *command = NULL;

  enc_subject = g_uri_escape_string (subject ? subject : "", NULL, FALSE);
  enc_body = g_uri_escape_string (body ? body : "", NULL, FALSE);

  url = g_string_new ("to=");

  g_string_append_printf (url, "'%s,'", address ? address : "");
  g_string_append_printf (url, ",subject=%s", enc_subject);
  g_string_append_printf (url, ",body=%s", enc_body);

  for (i = 0; attachments[i]; i++)
    g_string_append_printf (url, ",attachment=%s", attachments[i]);

  argv[0] = "thunderbird";
  argv[1] = "-compose";
  argv[2] = url->str;
  argv[3] = NULL;

  command = g_strjoinv (" ", (char **)argv);
  g_debug ("Running: %s\n", command);

  subprocess = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_NONE, error);

  return subprocess != NULL;
}

static gboolean
compose_mail (const char  *address,
              const char  *subject,
              const char  *body,
              const char **attachments,
              GError     **error)
{
        g_autoptr(GAppInfo) info = NULL;

        info = g_app_info_get_default_for_uri_scheme ("mailto");

        if (strstr (g_app_info_get_commandline (info), "thunderbird"))
                return compose_mail_thunderbird (address, subject,body, attachments, error);
        else
                return compose_mail_evolution (address, subject,body, attachments, error);
}

static gboolean
handle_compose_email (XdpImplEmail *object,
                      GDBusMethodInvocation *invocation,
                      const char *arg_handle,
                      const char *arg_app_id,
                      const char *arg_parent_window,
                      GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  g_autoptr(GError) error = NULL;
  const char *address = NULL;
  const char *subject = NULL;
  const char *body = NULL;
  const char *no_att[1] = { NULL };
  const char **attachments = no_att;
  guint response = 0;
  GVariantBuilder opt_builder;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_lookup (arg_options, "address", "&s", &address);
  g_variant_lookup (arg_options, "subject", "&s", &subject);
  g_variant_lookup (arg_options, "body", "&s", &body);
  g_variant_lookup (arg_options, "attachments", "^a&s", &attachments);

  if (!compose_mail (address, subject, body, attachments, NULL))
    response = 2;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_email_complete_compose_email (object,
                                         invocation,
                                         response,
                                         g_variant_builder_end (&opt_builder));

  return TRUE;
}

gboolean
email_init (GDBusConnection *bus,
            GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_email_skeleton_new ());

  g_signal_connect (helper, "handle-compose-email", G_CALLBACK (handle_compose_email), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
