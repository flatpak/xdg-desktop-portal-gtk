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

#include "screencast.h"

typedef struct _GnomeScreenCast GnomeScreenCast;
typedef struct _GnomeScreenCastSession GnomeScreenCastSession;
typedef struct _GnomeScreenCastStream GnomeScreenCastStream;

const char * gnome_screen_cast_session_get_stream_path_from_id (GnomeScreenCastSession *gnome_screen_cast_session,
                                                                uint32_t stream_id);

void gnome_screen_cast_session_add_stream_properties (GnomeScreenCastSession *gnome_screen_cast_session,
                                                      GVariantBuilder *streams_builder);

gboolean gnome_screen_cast_session_record_selections (GnomeScreenCastSession *gnome_screen_cast_session,
                                                      GVariant *selections,
                                                      ScreenCastSelection *select,
                                                      GError **error);

gboolean gnome_screen_cast_session_stop (GnomeScreenCastSession *gnome_screen_cast_session,
                                         GError **error);

gboolean gnome_screen_cast_session_start (GnomeScreenCastSession *gnome_screen_cast_session,
                                          GError **error);

GnomeScreenCastSession *gnome_screen_cast_create_session (GnomeScreenCast *gnome_screen_cast,
                                                          const char *remote_desktop_session_id,
                                                          GError **error);

int gnome_screen_cast_get_api_version (GnomeScreenCast *gnome_screen_cast);

GnomeScreenCast *gnome_screen_cast_new (GDBusConnection *connection);
