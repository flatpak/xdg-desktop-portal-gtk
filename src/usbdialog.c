/*
 * Copyright © 2024 GNOME Foundation Inc.
 * Copyright © 2026 Nuhiat Arefin <nuhiatarefin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Hubert Figuière <hub@figuiere.net>
 */

/* NOTE: This file is adapted from xdg-desktop-portal-gnome. */

#define _GNU_SOURCE 1

#include "config.h"

#include <string.h>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

#include "usbdialog.h"

#define INITIAL_LIST_SIZE 3

struct _UsbDialog
{
  GtkWindow parent;

  GtkWidget *scrolled_window;
  GtkWidget *heading;
  GtkWidget *device_list;
  GtkWidget *allow_usb_button;

  GVariant *devices;
  GtkWidget *more_row;
};

struct _UsbDialogClass
{
  GtkWindowClass parent_class;

  void (* response) (UsbDialog *dialog);
};

enum
{
  RESPONSE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (UsbDialog, usb_dialog, GTK_TYPE_WINDOW)

static void
usb_dialog_finalize (GObject *object)
{
  UsbDialog *self = USB_DIALOG (object);

  g_clear_pointer (&self->devices, g_variant_unref);

  G_OBJECT_CLASS (usb_dialog_parent_class)->finalize (object);
}

static gboolean
is_hex_digit (char c)
{
  return g_ascii_isxdigit (c);
}

static char
hex_value (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0;
}

static char *
parse_udev_string (const char *string)
{
  GString *parsed;
  const char *p;

  parsed = g_string_new (NULL);

  for (p = string; p && *p; p++)
    {
      if (p[0] == '\\' &&
          p[1] == 'x' &&
          p[2] != '\0' &&
          p[3] != '\0' &&
          is_hex_digit (p[2]) &&
          is_hex_digit (p[3]))
        {
          char value = (hex_value (p[2]) << 4) | hex_value (p[3]);

          g_string_append_c (parsed, value);
          p += 3;
        }
      else
        {
          g_string_append_c (parsed, *p);
        }
    }

  return g_string_free (parsed, FALSE);
}

static const char *
lookup_first_property (GHashTable  *props,
                       const char **keys)
{
  int i;

  for (i = 0; keys[i] != NULL; i++)
    {
      const char *value = g_hash_table_lookup (props, keys[i]);

      if (value && value[0])
        return value;
    }

  return NULL;
}

static void
add_serial_number (GtkWidget  *row_box,
                   const char *serial_number)
{
  GtkWidget *button;
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *title;
  GtkWidget *value;
  GtkWidget *image;
  g_autofree char *parsed_serial = NULL;

  parsed_serial = parse_udev_string (serial_number);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_show (box);

  title = gtk_label_new (_("Serial Number"));
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_widget_set_halign (title, GTK_ALIGN_START);
  gtk_widget_show (title);
  gtk_container_add (GTK_CONTAINER (box), title);

  value = gtk_label_new (parsed_serial);
  gtk_label_set_xalign (GTK_LABEL (value), 0.0);
  gtk_label_set_selectable (GTK_LABEL (value), TRUE);
  gtk_widget_set_halign (value, GTK_ALIGN_START);
  gtk_style_context_add_class (gtk_widget_get_style_context (value), "dim-label");
  gtk_widget_show (value);
  gtk_container_add (GTK_CONTAINER (box), value);

  popover = gtk_popover_new (NULL);
  gtk_container_add (GTK_CONTAINER (popover), box);

  button = gtk_menu_button_new ();
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_style_context_add_class (gtk_widget_get_style_context (button), "flat");
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (button), popover);
  image = gtk_image_new_from_icon_name ("dialog-information-symbolic",
                                        GTK_ICON_SIZE_BUTTON);
  gtk_widget_show (image);
  gtk_button_set_image (GTK_BUTTON (button), image);
  gtk_widget_show (button);

  gtk_container_add (GTK_CONTAINER (row_box), button);
}

static void
add_device_row (UsbDialog *self,
                GVariant  *device_info)
{
  static const char *vendor_keys[] = {
    "ID_VENDOR_FROM_DATABASE",
    "ID_VENDOR_ENC",
    "ID_VENDOR_ID",
    NULL
  };
  static const char *model_keys[] = {
    "ID_MODEL_FROM_DATABASE",
    "ID_MODEL_ENC",
    "ID_MODEL_ID",
    NULL
  };
  g_autoptr(GHashTable) props = NULL;
  GVariantIter iter;
  const char *key;
  GVariant *value;
  const char *vendor;
  const char *model;
  const char *serial_number;
  g_autofree char *title_text = NULL;
  g_autofree char *subtitle_text = NULL;
  GtkWidget *row;
  GtkWidget *row_box;
  GtkWidget *labels;
  GtkWidget *title;
  GtkWidget *subtitle;

  props = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  g_variant_iter_init (&iter, device_info);
  while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
    {
      g_autoptr(GVariant) value_variant = value;

      if (g_str_equal (key, "properties") &&
          g_variant_is_of_type (value_variant, G_VARIANT_TYPE ("a{sv}")))
        {
          GVariantIter props_iter;
          const char *prop_key;
          GVariant *prop_value;

          g_variant_iter_init (&props_iter, value_variant);
          while (g_variant_iter_next (&props_iter, "{&sv}", &prop_key, &prop_value))
            {
              g_autoptr(GVariant) prop_value_variant = prop_value;

              if (g_variant_is_of_type (prop_value_variant, G_VARIANT_TYPE_STRING))
                g_hash_table_insert (props,
                                     g_strdup (prop_key),
                                     g_variant_dup_string (prop_value_variant, NULL));
            }
        }
    }

  vendor = lookup_first_property (props, vendor_keys);
  model = lookup_first_property (props, model_keys);
  serial_number = g_hash_table_lookup (props, "ID_SERIAL_SHORT");
  if (!serial_number || !serial_number[0])
    serial_number = g_hash_table_lookup (props, "ID_SERIAL");

  title_text = model ? parse_udev_string (model) : g_strdup (_("Unknown device"));
  subtitle_text = vendor ? parse_udev_string (vendor) : g_strdup (_("Unknown vendor"));

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_show (row);

  row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (row_box, 12);
  gtk_widget_set_margin_bottom (row_box, 12);
  gtk_widget_set_margin_start (row_box, 12);
  gtk_widget_set_margin_end (row_box, 12);
  gtk_widget_show (row_box);

  labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand (labels, TRUE);
  gtk_widget_show (labels);
  gtk_container_add (GTK_CONTAINER (row_box), labels);

  title = gtk_label_new (title_text);
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign (title, GTK_ALIGN_FILL);
  gtk_widget_show (title);
  gtk_container_add (GTK_CONTAINER (labels), title);

  subtitle = gtk_label_new (subtitle_text);
  gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (subtitle), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign (subtitle, GTK_ALIGN_FILL);
  gtk_style_context_add_class (gtk_widget_get_style_context (subtitle), "dim-label");
  gtk_widget_show (subtitle);
  gtk_container_add (GTK_CONTAINER (labels), subtitle);

  if (serial_number && serial_number[0])
    add_serial_number (row_box, serial_number);

  gtk_container_add (GTK_CONTAINER (row), row_box);
  gtk_container_add (GTK_CONTAINER (self->device_list), row);
}

static void
show_more (UsbDialog *self)
{
  GVariantIter iter;
  const char *id G_GNUC_UNUSED;
  GVariant *device_info;
  GVariant *access_options;
  gsize i = 0;

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_window_set_resizable (GTK_WINDOW (self), TRUE);
  gtk_widget_hide (self->more_row);

  g_return_if_fail (g_variant_n_children (self->devices) > INITIAL_LIST_SIZE);

  g_variant_iter_init (&iter, self->devices);
  while (g_variant_iter_next (&iter,
                              "(&s@a{sv}@a{sv})",
                              &id,
                              &device_info,
                              &access_options))
    {
      if (i >= INITIAL_LIST_SIZE)
        add_device_row (self, device_info);

      i++;
      g_variant_unref (device_info);
      g_variant_unref (access_options);
    }
}

static void
more_pressed (GtkGestureMultiPress *gesture,
              int                   n_press,
              double                x,
              double                y,
              UsbDialog            *self)
{
  (void) gesture;
  (void) x;

  if (n_press != 1)
    return;

  if (gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->device_list), y) == GTK_LIST_BOX_ROW (self->more_row))
    show_more (self);
}

static void
add_more_row (UsbDialog *self)
{
  GtkWidget *row;
  GtkWidget *image;
  GtkGesture *gesture;

  row = gtk_list_box_row_new ();

  gesture = gtk_gesture_multi_press_new (self->device_list);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), FALSE);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_BUBBLE);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);

  g_signal_connect (gesture, "pressed", G_CALLBACK (more_pressed), self);

  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
  image = gtk_image_new_from_icon_name ("view-more-symbolic", GTK_ICON_SIZE_BUTTON);
  g_object_set (image, "margin", 14, NULL);
  gtk_container_add (GTK_CONTAINER (row), image);
  gtk_widget_show (row);
  gtk_widget_show (image);
  gtk_container_add (GTK_CONTAINER (self->device_list), row);
  self->more_row = row;
}

static gboolean
usb_dialog_delete_event (GtkWidget   *dialog,
                         GdkEventAny *event)
{
  (void) event;

  gtk_widget_hide (dialog);

  g_signal_emit (dialog, signals[RESPONSE], 0, GTK_RESPONSE_CANCEL);

  return TRUE;
}

static void
allow_usb_button_clicked (UsbDialog *self)
{
  g_signal_emit (self, signals[RESPONSE], 0, GTK_RESPONSE_APPLY);
}

static void
deny_usb_button_clicked (UsbDialog *self)
{
  g_signal_emit (self, signals[RESPONSE], 0, GTK_RESPONSE_CANCEL);
}

static void
usb_dialog_init (UsbDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_window_set_default (GTK_WINDOW (self), self->allow_usb_button);
}

static void
usb_dialog_class_init (UsbDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = usb_dialog_finalize;
  widget_class->delete_event = usb_dialog_delete_event;

  signals[RESPONSE] = g_signal_new ("response",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL,
                                    NULL,
                                    G_TYPE_NONE, 1, G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/usbdialog.ui");

  gtk_widget_class_bind_template_child (widget_class, UsbDialog, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, UsbDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, UsbDialog, device_list);
  gtk_widget_class_bind_template_child (widget_class, UsbDialog, allow_usb_button);

  gtk_widget_class_bind_template_callback (widget_class, allow_usb_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, deny_usb_button_clicked);
}

UsbDialog *
usb_dialog_new (const char *app_id,
                GVariant   *devices)
{
  UsbDialog *self;
  g_autofree char *heading = NULL;
  GVariantIter iter;
  const char *id G_GNUC_UNUSED;
  GVariant *device_info;
  GVariant *access_options;
  gsize n_devices;
  gsize i = 0;

  self = g_object_new (USB_TYPE_DIALOG, NULL);
  self->devices = g_variant_ref (devices);

  if (app_id && strcmp (app_id, "") != 0)
    {
      g_autoptr(GAppInfo) info = NULL;
      g_autofree char *desktop_id = NULL;
      const char *display_name = app_id;

      desktop_id = g_strconcat (app_id, ".desktop", NULL);
      info = G_APP_INFO (g_desktop_app_info_new (desktop_id));
      if (info)
        display_name = g_app_info_get_display_name (info);

      heading = g_strdup_printf (_("%s wants to access the following USB devices"), display_name);
    }
  else
    {
      heading = g_strdup (_("An app wants to access the following USB devices"));
    }

  gtk_label_set_label (GTK_LABEL (self->heading), heading);

  n_devices = g_variant_n_children (devices);
  g_variant_iter_init (&iter, devices);
  while (g_variant_iter_next (&iter,
                              "(&s@a{sv}@a{sv})",
                              &id,
                              &device_info,
                              &access_options))
    {
      if (i < INITIAL_LIST_SIZE)
        add_device_row (self, device_info);

      i++;
      g_variant_unref (device_info);
      g_variant_unref (access_options);
    }

  if (n_devices > INITIAL_LIST_SIZE)
    add_more_row (self);

  return self;
}
