/*
 * Copyright © 2018 Red Hat, Inc
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
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>

typedef struct _GnomeScreenCast GnomeScreenCast;
typedef struct _GnomeScreenCastSession GnomeScreenCastSession;
typedef struct _GnomeScreenCastStream GnomeScreenCastStream;

uint32_t gnome_screen_cast_stream_get_pipewire_node_id (GnomeScreenCastStream *stream);

gboolean gnome_screen_cast_stream_get_position (GnomeScreenCastStream *stream,
                                                int *x,
                                                int *y);

gboolean gnome_screen_cast_stream_get_size (GnomeScreenCastStream *stream,
                                            int *width,
                                            int *height);

GList *gnome_screen_cast_session_get_streams (GnomeScreenCastSession *gnome_screen_cast_session);

gboolean gnome_screen_cast_session_record_monitor (GnomeScreenCastSession *gnome_screen_cast_session,
                                                   const char *connector,
                                                   GError **error);

gboolean gnome_screen_cast_session_stop (GnomeScreenCastSession *gnome_screen_cast_session,
                                         GError **error);

gboolean gnome_screen_cast_session_start (GnomeScreenCastSession *gnome_screen_cast_session,
                                          GError **error);

GnomeScreenCastSession *gnome_screen_cast_create_session (GnomeScreenCast *gnome_screen_cast,
                                                          GError **error);

GnomeScreenCast *gnome_screen_cast_new (GDBusConnection *connection);
