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
#include "emailtb.h"
#include "emailtbdialog.h"
#include "request.h"
#include "utils.h"
#include "externalwindow.h"

typedef struct {
  XdpImplEmail *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int response;
  GVariant *arg_options; // origin of variable cc, bcc,...
  const char **attachments;
  const char * const *addrs;
  const char * const *cc;
  const char * const *bcc;
  const char *subject;
  const char *body;
} EmailTbDialogHandle;

static void
email_tb_dialog_handle_free (gpointer data)
{
  EmailTbDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  g_variant_unref (handle->arg_options);
  g_strfreev ((gchar **)handle->addrs);

  g_free (handle);
}

static void
email_tb_dialog_handle_close (EmailTbDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  email_tb_dialog_handle_free (handle);
}

static void
send_response (EmailTbDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_email_complete_compose_email (handle->impl,
                                         handle->invocation,
                                         handle->response,
                                         g_variant_builder_end (&opt_builder));

  email_tb_dialog_handle_close (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              EmailTbDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_email_complete_compose_email (handle->impl,
                                         handle->invocation,
                                         2,
                                         g_variant_builder_end (&opt_builder));
  email_tb_dialog_handle_close (handle);

  return FALSE;
}

char*
clean_up_parent_path_from_file_path (const char* input)
{
  /* Replace path-element /../ to make the path bedder readable. Example:
   *   Input:  "/home/user/file-ok.txt        /../user/secret.txt"
   *   Output: "/home/user/secret.txt"
   *   This function only works with absolute pathes: It must start with "/".
   *   Not supported are: relative pathes or URI-pathes (e.g. file:// or percent-encoded)
  */

  if (input[0] == '\0') return "";
  if (input[0] != '/' )
    {
      g_warning ("attachment: Error in %s with %s (must start with /)", __func__, input);
      return "";
    }

  GString *out = g_string_new ("");
  char ** splited = g_strsplit (input, "/", -1);
  int i = 0;
  for (i = 0; splited[i]; i++)
    {
      // search for path ..
      if (splited[i][0] == '.' && splited[i][1] == '.' && splited[i][2] == '\0')
        {
          splited[i] = "";
          int j = 0;
          gboolean remove_done = FALSE;
          // search & remove the path part before
          for (j = i; j > 0; j--)
            {
              if (splited[j][0] != '\0')
                {
                  splited[j] = "";
                  remove_done = TRUE;
                  break;
                }
            }
          if (!remove_done)
            {
              g_warning ("attachment: Error in %s with %s (seems invalid)", __func__, input);
              return "";
            }
        }
    }
  // pack new string
  for (i = 0; splited[i]; i++)
    {
      if (splited[i][0] != '\0')
        {
          g_string_append (out, "/");
          g_string_append (out, splited[i]);
        }
    }
  return g_string_free (out, FALSE);
}

const char *
get_uri_for_tb (const char *input,
                gboolean check_exists)
{
    if (input == NULL || input[0] == '\0')
    {
      g_warning ("attachment: skipped this file (does not exists): %s ", input);
      return "";
    }
    if (check_exists && !g_file_test (input, G_FILE_TEST_EXISTS))
      {
        g_warning ("attachment: skipped this file (does not exists): %s ", input);
        return "";
      }
    gchar *file_uri = g_filename_to_uri (input, NULL, NULL);
    if (file_uri == NULL)
      {
        g_warning ("failed to convert to URI: %s", input);
        return "";
      }
    GString *file_esc = g_string_new ((char *) file_uri);
    g_string_replace (file_esc, "\'", "%27", 0);
    // escape ' (this is only for a very rare problem: e.g. attach this two filenames, in this exact order: a' and abc)
    g_string_replace (file_esc, ",", "%2C", 0); // escape , (needed to handle multiple files)
    if (check_exists && !g_file_test (g_filename_from_uri (file_esc->str, NULL, NULL), G_FILE_TEST_EXISTS))
      {
        // this check is just a dublicate (to check before), and can be removed
        g_warning ("attachment: skipped this file (does not exists): %s ", file_esc->str);
        g_string_free (file_esc, TRUE);
        return "";
      }
    return g_string_free (file_esc, FALSE);
}

const char *
escape_maybe_single_quote_comma (const char* input)
{
  /* This is just for future use:
   *  In Thunderbird the string ', in E-Mail-adress is a problematic combination.
   *  Seems, that actually character , in E-Mail-adress is not allowed (in xdg-desktop).
   *  So, this is not a real problem, at the moment.
   */
  if (strchr (input, ',') != NULL && strchr (input, '\'') != NULL)
    {
      GString *s = g_string_new (input);
      g_string_replace (s, "\'", "\\'", 0);
      g_string_replace (s, ",", "\\,", 0);
      return g_string_free (s, FALSE);
    }
  else
    return input;
}

gboolean
compose_mail_thunderbird (GAppInfo       *info,
                          const char * const *addrs,
                          const char * const *cc,
                          const char * const *bcc,
                          const char     *subject,
                          const char     *body,
                          const char    **attachments,
                          GError        **error)
{
  g_autofree char *enc_subject = NULL;
  g_autofree GString *enc1_body = g_string_new ("");
  g_autofree char *enc2_body = NULL;
  g_autofree char *arg = NULL;
  g_autoptr(GString) tb_arg = NULL;
  int i;
  gboolean success;
  const char *is_tb_snap;
  const char *old_cmdline;

  tb_arg = g_string_new ("");

  enc_subject = g_uri_escape_string (subject ? subject : "", NULL, FALSE);

  // Thunderbird would interpret in body HTML-Code (only in this start-way ; not by mailto:).
  // Maybe just characters < > are required to be replaced.
  if (body)
    for (i = 0; body[i]; i++)
      {
        switch (body[i])
          {
          case '<':
            g_string_append (enc1_body, "&lt;");
            break;
          case '>':
            g_string_append (enc1_body, "&gt;");
            break;
          case '&':
            g_string_append (enc1_body, "&amp;");
            break;
          case '"':
            g_string_append (enc1_body, "&quot;");
            break;
          case '\'':
            g_string_append (enc1_body, "&apos;");
            break;
          default:
            g_string_append_c (enc1_body, body[i]);
          }
      }
  enc2_body = g_uri_escape_string (enc1_body->str ? enc1_body->str : "", NULL, FALSE);

  /* First start with attachment:
   *   This may prevent malicous input data. Example:
   *     Previous used of attachment= within an Email-Adresse.
   *     That shouldn't work, but just in case. */
  if (attachments)
    {
      g_string_append_printf (tb_arg, "attachment='");
      int files_added = 0;
      for (i = 0; attachments[i]; i++)
        {
          if (attachments[i][0] == '\0')
            continue; // skip empty entrys (e.g. because file not found before)
          files_added++;
          if (files_added != 1)
            g_string_append_printf (tb_arg, ",");
          g_string_append_printf (tb_arg, "%s", attachments[i]);
        }
        g_string_append_printf(tb_arg, "',");
    }

  if (addrs)
    {
      g_string_append_printf (tb_arg, "to='");
      for (i = 0; addrs[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (addrs[i]));
      g_string_append_printf (tb_arg, "',");
    }

  if (cc)
    {
      g_string_append_printf (tb_arg, "cc='");
      for (i = 0; cc[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (cc[i]));
      g_string_append_printf (tb_arg, "',");
    }

  if (bcc)
    {
      g_string_append_printf (tb_arg, "bcc='");
      for (i = 0; bcc[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (bcc[i]));
      g_string_append_printf (tb_arg, "',");
    }

  g_string_append_printf (tb_arg, "subject='%s',", enc_subject);
  g_string_append_printf (tb_arg, "body=%s,", enc2_body);

  /* The commandline can be complicated, e.g.
   * env BAMF_DESKTOP_FILE_HINT=/var/lib/snapd/.../thunderbird_thunderbird.desktop /snap/bin/thunderbird %u
   * so just match for thunderbird.
   */
  old_cmdline = g_app_info_get_commandline (info);
  is_tb_snap = strstr (old_cmdline, "/snap/bin/thunderbird");
  gchar *cmdline[] = {
      is_tb_snap ? "/snap/bin/thunderbird" : "thunderbird",
      "-compose",
      tb_arg->str,
      NULL
  };

  g_debug ("Launching: %s %s %s", cmdline[0], cmdline[1], cmdline[2]);
  success = g_spawn_async (NULL, cmdline, NULL, G_SPAWN_SEARCH_PATH, NULL,
                           NULL, NULL, error);
  return success;
}

static const char **
process_attachment_list (const char ** attachments,
                         gboolean output_uri,
                         gboolean check_exists_uri)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  int i = 0;
  for (i = 0; attachments[i]; i++)
    {
      const char *file =  clean_up_parent_path_from_file_path (attachments[i]);
      if (output_uri)
        {
          const char *uri = get_uri_for_tb (file, check_exists_uri);
          g_strv_builder_add (builder, uri);
        }
      else
        g_strv_builder_add (builder, file);
    }
  return (const char **) g_strv_builder_end (builder);
}

static void
email_tb_dialog_done (GtkWidget *widget,
                      int response,
                      gpointer user_data)
{
  EmailTbDialogHandle *handle = user_data;
  const char *no_att[1] = { NULL };

  switch (response)
    {
      default:
        g_warning ("Unexpected response: %d", response);
       /* Fall through */
      case GTK_RESPONSE_DELETE_EVENT:
        /* Fall through */
      case GTK_RESPONSE_CANCEL:
        handle->response = 1;
        handle->attachments = no_att;
        break;

      case GTK_RESPONSE_NO:
        handle->response = 1;
        handle->attachments = no_att;
        break;

      case GTK_RESPONSE_YES:
        handle->response = 0;
        break;
    }

  if (response == GTK_RESPONSE_NO || response == GTK_RESPONSE_YES)
    {
      g_autoptr(GAppInfo) info = NULL;
      info = g_app_info_get_default_for_uri_scheme ("mailto");
      const char ** attachments_processed_and_exists = process_attachment_list (handle->attachments, TRUE, TRUE);
      if (compose_mail ((const char * const *)handle->addrs, handle->cc, handle->bcc, handle->subject, handle->body,
                        attachments_processed_and_exists, NULL))
        handle->response = 0;
      else
        handle->response = 2;
      g_strfreev ((gchar **)attachments_processed_and_exists);
    }
  send_response (handle);
}

gboolean
handle_compose_email_tb (XdpImplEmail *object,
                         GDBusMethodInvocation *invocation,
                         const char *arg_handle,
                         const char *arg_app_id,
                         const char *arg_parent_window,
                         GVariant *arg_options,
                         const char * const *addrs,
                         const char * const *cc,
                         const char * const *bcc,
                         const char  *subject,
                         const char  *body,
                         const char **attachments)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  GVariantBuilder opt_builder;
  guint response = 0;
  EmailTbDialogHandle *handle;
  g_autoptr(GError) error = NULL;

  GtkWidget *dialog;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  const char *no_att[1] = { NULL };
  g_autoptr(GAppInfo) info = NULL;
  gboolean is_thunderbird = FALSE;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  info = g_app_info_get_default_for_uri_scheme ("mailto");

  if (info == NULL)
    {
      g_warning ("ERROR: No mailto handler");
      return FALSE;
    }

  const char *commandline;
  commandline = g_app_info_get_commandline (info);
  if (strstr (commandline, "thunderbird") )
    is_thunderbird = TRUE;

  if (attachments == no_att || !is_thunderbird)
    {
      if (!compose_mail ((const char * const *)addrs, cc, bcc, subject, body, attachments, NULL))
        response = 2;

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      xdp_impl_email_complete_compose_email (object,
                                             invocation,
                                             response,
                                             g_variant_builder_end (&opt_builder));

      return TRUE;
    }
  else
    {
      g_debug ("Show attachment confirm Dialog (only for Thunderbird needed)");
      if (arg_parent_window)
        {
          external_parent = create_external_window_from_handle (arg_parent_window);
          if (!external_parent)
            g_warning ("Failed to associate portal window with parent window %s",
                       arg_parent_window);
        }

      if (external_parent)
        display = external_window_get_display (external_parent);
      else
        display = gdk_display_get_default ();
      screen = gdk_display_get_default_screen (display);

      fake_parent = g_object_new (GTK_TYPE_WINDOW,
                                  "type", GTK_WINDOW_TOPLEVEL,
                                  "screen", screen,
                                  NULL);
      g_object_ref_sink (fake_parent);

      const char **attachments_list_text =  process_attachment_list (attachments, FALSE, FALSE);
      const char **attachments_list_link =  process_attachment_list (attachments, TRUE, FALSE);

      dialog = GTK_WIDGET (email_tb_dialog_new (attachments_list_text, attachments_list_link));
      gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
      gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

      handle = g_new0 (EmailTbDialogHandle, 1);
      handle->impl = object;
      handle->invocation = invocation;
      handle->request = g_object_ref (request);
      handle->dialog = g_object_ref (dialog);
      handle->external_parent = external_parent;
      handle->arg_options = g_variant_ref (arg_options);
      handle->attachments = attachments;
      handle->addrs = (const char * const *) g_strdupv ((gchar **)addrs);
      handle->cc = cc;
      handle->bcc = bcc;
      handle->subject = subject;
      handle->body = body;

      g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);
      g_signal_connect (dialog, "done", G_CALLBACK (email_tb_dialog_done), handle);

      gtk_widget_realize (dialog);

      if (external_parent)
        external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

      gtk_widget_show (dialog);

      request_export (request, g_dbus_method_invocation_get_connection (invocation));
      g_strfreev ((gchar **)attachments_list_text);
      g_strfreev ((gchar **)attachments_list_link);
    }
  return TRUE;
}
