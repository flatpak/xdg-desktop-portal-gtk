#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "request.h"
#include "utils.h"
#include "externalwindow.h"

typedef struct {
  XdpImplAccess *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;
  GHashTable *choices;

  int response;
} AccessDialogHandle;

static void
access_dialog_handle_free (gpointer data)
{
  AccessDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  if (handle->choices)
    g_hash_table_unref (handle->choices);

  g_free (handle);
}

static void
access_dialog_handle_close (AccessDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  access_dialog_handle_free (handle);
}

static void
send_response (AccessDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (handle->request->exported)
    request_unexport (handle->request);

  if (handle->response == 0 && handle->choices != NULL)
    {
      GVariantBuilder choice_builder;
      GHashTableIter iter;
      const char *key;
      GtkWidget *widget;

      g_variant_builder_init (&choice_builder, G_VARIANT_TYPE_VARDICT);
      g_hash_table_iter_init (&iter, handle->choices);
      while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&widget))
        {
          if (GTK_IS_RADIO_BUTTON (widget))
            {
              gchar **str;

              if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
                continue;

              str = g_strsplit (key, ":", -1);
              g_variant_builder_add (&choice_builder, "{sv}", str[0], g_variant_new_string (str[1]));
              g_strfreev (str);
            }
          else if (GTK_IS_CHECK_BUTTON (widget))
            {
              gboolean active;

              active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
              g_variant_builder_add (&choice_builder, "{sv}", key, g_variant_new_string (active ? "true" : "false"));
            }
        }

      g_variant_builder_add (&opt_builder, "{sv}", "choices", g_variant_builder_end (&choice_builder));
    }

  xdp_impl_access_complete_access_dialog (handle->impl,
                                          handle->invocation,
                                          handle->response,
                                          g_variant_builder_end (&opt_builder));

  access_dialog_handle_close (handle);
}

static void
access_dialog_response (GtkWidget *widget,
                        int response,
                        gpointer user_data)
{
  AccessDialogHandle *handle = user_data;

  switch (response)
    {
    default:
      g_warning ("Unexpected response: %d", response);
      /* Fall through */
    case GTK_RESPONSE_DELETE_EVENT:
      handle->response = 2;
      break;

    case GTK_RESPONSE_CANCEL:
      handle->response = 1;
      break;

    case GTK_RESPONSE_OK:
      handle->response = 0;
      break;
    }

  send_response (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              AccessDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_access_complete_access_dialog (handle->impl,
                                          handle->invocation,
                                          2,
                                          g_variant_builder_end (&opt_builder));
  access_dialog_handle_close (handle);
  return FALSE;
}

static void
add_choice (GtkWidget *box,
            GVariant *choice,
            GHashTable *table)
{
  const char *id;
  const char *name;
  g_autoptr(GVariant) options = NULL;
  const char *selected;

  g_variant_get (choice, "(&s&s@a(ss)&s)", &id, &name, &options, &selected);

  if (g_variant_n_children (options) == 0)
    {
      GtkWidget *button;

      button = gtk_check_button_new_with_label (name);
      g_object_set (button, "margin-top", 10, NULL);
      gtk_widget_show (button);
      if (strcmp (selected, "true") == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
      gtk_container_add (GTK_CONTAINER (box), button);
      g_hash_table_insert (table, g_strdup (id), button);
    }
  else
    {
      GtkWidget *label;
      GtkWidget *group = NULL;
      int i;

      label = gtk_label_new (name);
      gtk_widget_show (label);
      gtk_widget_set_halign (label, GTK_ALIGN_START);
      g_object_set (label, "margin-top", 10, NULL);
      gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
      gtk_container_add (GTK_CONTAINER (box), label);

      for (i = 0; i < g_variant_n_children (options); i++)
        {
          const char *option_id;
          const char *option_name;
          GtkWidget *radio;

          g_variant_get_child (options, i, "(&s&s)", &option_id, &option_name);

          radio = gtk_radio_button_new_with_label (NULL, option_name);
          gtk_widget_show (radio);

          g_hash_table_insert (table, g_strconcat (id, ":", option_id, NULL), radio);

          if (group)
            gtk_radio_button_join_group (GTK_RADIO_BUTTON (radio),
                                         GTK_RADIO_BUTTON (group));
          else
            group = radio;
          gtk_container_add (GTK_CONTAINER (box), radio);
          if (strcmp (selected, option_id) == 0)
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
        }
    }
}

static void
fix_up_label_alignment (GtkWidget *area)
{
  GList *children, *l;

  /* Hack! Necessary because we're (mis)using GtkMessageDialog */
  children = gtk_container_get_children (GTK_CONTAINER (area));
  for (l = children; l; l = l->next)
    {
      GtkWidget *child = l->data;
      gtk_widget_set_halign (child, GTK_ALIGN_START);
    }
  g_list_free (children);
}

static gboolean
handle_access_dialog (XdpImplAccess *object,
                      GDBusMethodInvocation *invocation,
                      const char *arg_handle,
                      const char *arg_app_id,
                      const char *arg_parent_window,
                      const char *arg_title,
                      const char *arg_subtitle,
                      const char *arg_body,
                      GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  AccessDialogHandle *handle;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  gboolean modal;
  GtkWidget *dialog;
  const char *deny_label;
  const char *grant_label;
  const char *icon;
  g_autoptr(GVariant) choices = NULL;
  GtkWidget *area;
  GtkWidget *image;
  GHashTable *choice_table = NULL;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  if (!g_variant_lookup (arg_options, "modal", "b", &modal))
    modal = TRUE;

  if (!g_variant_lookup (arg_options, "deny_label", "&s", &deny_label))
    deny_label = _("Deny Access");

  if (!g_variant_lookup (arg_options, "grant_label", "&s", &grant_label))
    grant_label = _("Grant Access");

  if (!g_variant_lookup (arg_options, "icon", "&s", &icon))
    icon = NULL;

  choices = g_variant_lookup_value (arg_options, "choices", G_VARIANT_TYPE ("a(ssa(ss)s)"));

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

  dialog = gtk_message_dialog_new (NULL,
                                   0,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   arg_title,
                                   NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
  gtk_window_set_modal (GTK_WINDOW (dialog), modal);
  gtk_dialog_add_button (GTK_DIALOG (dialog), deny_label, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), grant_label, GTK_RESPONSE_OK);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", arg_subtitle);

  area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
  fix_up_label_alignment (area);

  if (choices)
    {
      int i;

      choice_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      for (i = 0; i < g_variant_n_children (choices); i++)
        add_choice (area, g_variant_get_child_value (choices, i), choice_table);
    }

  if (!g_str_equal (arg_body, ""))
    {
      GtkWidget *body_label;

      body_label = gtk_label_new (arg_body);
      gtk_widget_set_halign (body_label, GTK_ALIGN_START);
      g_object_set (body_label, "margin-top", 10, NULL);
#if GTK_CHECK_VERSION (3,16,0)
      gtk_label_set_xalign (GTK_LABEL (body_label), 0);
#else
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      /* This is deprecated in 3.14, but gtk_label_set_xalign() is not
       * available until 3.16. */
      gtk_misc_set_alignment (GTK_MISC (body_label), 0.0, 0.5);
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
      gtk_label_set_line_wrap (GTK_LABEL (body_label), TRUE);
      gtk_label_set_max_width_chars (GTK_LABEL (body_label), 50);
      gtk_widget_show (body_label);
      gtk_container_add (GTK_CONTAINER (area), body_label);
    }

  image = gtk_image_new_from_icon_name (icon ? icon : "image-missing", GTK_ICON_SIZE_DIALOG);
  gtk_widget_set_opacity (image, icon ? 1.0 : 0.0);
  gtk_widget_show (image);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), image);
G_GNUC_END_IGNORE_DEPRECATIONS

  handle = g_new0 (AccessDialogHandle, 1);
  handle->impl = object;
  handle->invocation = invocation;
  handle->request = g_object_ref (request);
  handle->dialog = g_object_ref (dialog);
  handle->external_parent = external_parent;
  handle->choices = choice_table;

  g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

  g_signal_connect (dialog, "response", G_CALLBACK (access_dialog_response), handle);

  gtk_widget_realize (dialog);

  if (external_parent)
    external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

  gtk_widget_show (dialog);

  request_export (request, g_dbus_method_invocation_get_connection (invocation));

  return TRUE;
}

gboolean
access_init (GDBusConnection *bus,
             GError **error)
{
  GDBusInterfaceSkeleton *helper;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_access_skeleton_new ());

  g_signal_connect (helper, "handle-access-dialog", G_CALLBACK (handle_access_dialog), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
