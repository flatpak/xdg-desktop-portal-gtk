/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include "gsd-remote-display-manager.h"

enum
{
    PROP_0,
    PROP_FORCE_DISABLE_ANIMATIONS
};

struct _GsdRemoteDisplayManager {
        GObject parent;

        /* Proxy for the force-disable-animations property */
        gboolean      disabled;

        GDBusProxy   *vino_proxy;
        GCancellable *cancellable;
        guint         vino_watch_id;
        gboolean      vnc_in_use;
};

static void     gsd_remote_display_manager_class_init  (GsdRemoteDisplayManagerClass *klass);
static void     gsd_remote_display_manager_init        (GsdRemoteDisplayManager      *remote_display_manager);

G_DEFINE_TYPE (GsdRemoteDisplayManager, gsd_remote_display_manager, G_TYPE_OBJECT)

static void
update_property_from_variant (GsdRemoteDisplayManager *manager,
                              GVariant                *variant)
{
        manager->vnc_in_use = g_variant_get_boolean (variant);
        manager->disabled = manager->vnc_in_use;

        g_debug ("%s because of remote display status (vnc: %d)",
                 manager->disabled ? "Disabling" : "Enabling",
                 manager->vnc_in_use);
        g_object_notify (G_OBJECT (manager), "force-disable-animations");
}

static void
props_changed (GDBusProxy              *proxy,
               GVariant                *changed_properties,
               GStrv                    invalidated_properties,
               GsdRemoteDisplayManager *manager)
{
        GVariant *v;

        v = g_variant_lookup_value (changed_properties, "Connected", G_VARIANT_TYPE_BOOLEAN);
        if (v) {
                g_debug ("Received connected change");
                update_property_from_variant (manager, v);
                g_variant_unref (v);
        }
}

static void
got_vino_proxy (GObject                 *source_object,
                GAsyncResult            *res,
                GsdRemoteDisplayManager *manager)
{
        GError *error = NULL;
        GVariant *v;

        manager->vino_proxy = g_dbus_proxy_new_finish (res, &error);
        if (manager->vino_proxy == NULL) {
                g_warning ("Failed to get Vino's D-Bus proxy: %s", error->message);
                g_error_free (error);
                return;
        }

        g_signal_connect (manager->vino_proxy, "g-properties-changed",
                          G_CALLBACK (props_changed), manager);

        v = g_dbus_proxy_get_cached_property (manager->vino_proxy, "Connected");
        if (v) {
                g_debug ("Setting original state");
                update_property_from_variant (manager, v);
                g_variant_unref (v);
        }
}

static void
vino_appeared_cb (GDBusConnection         *connection,
                  const gchar             *name,
                  const gchar             *name_owner,
                  GsdRemoteDisplayManager *manager)
{
        g_debug ("Vino appeared");
        g_dbus_proxy_new (connection,
                          G_DBUS_PROXY_FLAGS_NONE,
                          NULL,
                          name,
                          "/org/gnome/vino/screens/0",
                          "org.gnome.VinoScreen",
                          manager->cancellable,
                          (GAsyncReadyCallback) got_vino_proxy,
                          manager);
}

static void
vino_vanished_cb (GDBusConnection         *connection,
                  const char              *name,
                  GsdRemoteDisplayManager *manager)
{
        g_debug ("Vino vanished");
        if (manager->cancellable != NULL) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }
        g_clear_object (&manager->vino_proxy);

        /* And reset for us to have animations */
        manager->disabled = FALSE;
        g_object_notify (G_OBJECT (manager), "force-disable-animations");
}

static gboolean
gsd_display_has_extension (const gchar *ext)
{
        int op, event, error;
        GdkDisplay *display;

        display = gdk_display_get_default ();
        if (!GDK_IS_X11_DISPLAY (display))
                return FALSE;

        return XQueryExtension (gdk_x11_get_default_xdisplay (),
                                ext, &op, &event, &error);
}

static gboolean
gsd_display_has_llvmpipe (void)
{
        glong is_software_rendering_atom;
        Atom type;
        gint format;
        gulong nitems;
        gulong bytes_after;
        guchar *data;
        GdkDisplay *display;

        if (g_getenv ("GSD_ignore_llvmpipe") != NULL)
                return FALSE;

        display = gdk_display_get_default ();
        if (!GDK_IS_X11_DISPLAY (display))
                return FALSE;

        is_software_rendering_atom = gdk_x11_get_xatom_by_name_for_display (display, "_GNOME_IS_SOFTWARE_RENDERING");
        gdk_x11_display_error_trap_push (display);
        XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display),  gdk_x11_get_default_root_xwindow (),
                            is_software_rendering_atom,
                            0, G_MAXLONG, False, XA_CARDINAL, &type, &format, &nitems,
                            &bytes_after, &data);
        gdk_x11_display_error_trap_pop_ignored (display);

        if (type == XA_CARDINAL) {
               glong *is_accelerated_ptr = (glong*) data;

               return (*is_accelerated_ptr == 1);
        }

        return FALSE;
}

static void
gsd_remote_display_manager_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
        GsdRemoteDisplayManager *manager;

        manager = GSD_REMOTE_DISPLAY_MANAGER (object);

        switch (prop_id) {
        case PROP_FORCE_DISABLE_ANIMATIONS:
                g_value_set_boolean (value, manager->disabled);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_remote_display_manager_panel_finalize (GObject *object)
{
        GsdRemoteDisplayManager *manager = GSD_REMOTE_DISPLAY_MANAGER (object);
        g_debug ("Stopping remote_display manager");

        if (manager->vino_watch_id > 0) {
                g_bus_unwatch_name (manager->vino_watch_id);
                manager->vino_watch_id = 0;
        }

        if (manager->cancellable != NULL) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }
        g_clear_object (&manager->vino_proxy);
}

static void
gsd_remote_display_manager_class_init (GsdRemoteDisplayManagerClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GParamSpec      *pspec;

        object_class->get_property = gsd_remote_display_manager_get_property;
        object_class->finalize = gsd_remote_display_manager_panel_finalize;

        pspec = g_param_spec_boolean ("force-disable-animations",
                                      "Force disable animations",
                                      "Force disable animations",
                                      FALSE,
                                      G_PARAM_READABLE);
        g_object_class_install_property (object_class, PROP_FORCE_DISABLE_ANIMATIONS, pspec);
}

static void
gsd_remote_display_manager_init (GsdRemoteDisplayManager *manager)
{

        manager->cancellable = g_cancellable_new ();

        g_debug ("Starting remote-display manager");

        /* Xvnc exposes an extension named VNC-EXTENSION */
        if (gsd_display_has_extension ("VNC-EXTENSION")) {
                g_debug ("Disabling animations because VNC-EXTENSION was detected");
                manager->disabled = TRUE;
                g_object_notify (G_OBJECT (manager), "force-disable-animations");
                return;
        }

	/* disable animations if running under llvmpipe */
	if (gsd_display_has_llvmpipe ()) {
		g_debug ("Disabling animations because llvmpipe was detected");
		manager->disabled = TRUE;
		g_object_notify (G_OBJECT (manager), "force-disable-animations");
		return;
	}

        /* Monitor Vino's usage */
        manager->vino_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                   "org.gnome.Vino",
                                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                   (GBusNameAppearedCallback) vino_appeared_cb,
                                                   (GBusNameVanishedCallback) vino_vanished_cb,
                                                   manager, NULL);
}

GsdRemoteDisplayManager *
gsd_remote_display_manager_new (void)
{
        return GSD_REMOTE_DISPLAY_MANAGER (g_object_new (GSD_TYPE_REMOTE_DISPLAY_MANAGER, NULL));
}
