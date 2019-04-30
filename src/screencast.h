/*
 * Copyright © 2017 Red Hat, Inc
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

typedef enum _ScreenCastSourceType
{
  SCREEN_CAST_SOURCE_TYPE_MONITOR = 1,
  SCREEN_CAST_SOURCE_TYPE_WINDOW = 2,
} ScreenCastSourceType;

typedef enum _ScreenCastCursorMode
{
  SCREEN_CAST_CURSOR_MODE_NONE = 0,
  SCREEN_CAST_CURSOR_MODE_HIDDEN = 1,
  SCREEN_CAST_CURSOR_MODE_EMBEDDED = 2,
  SCREEN_CAST_CURSOR_MODE_METADATA = 4,
} ScreenCastCursorMode;

typedef struct _ScreenCastSelection
{
  gboolean multiple;
  ScreenCastCursorMode cursor_mode;
} ScreenCastSelection;

gboolean screen_cast_init (GDBusConnection *connection,
                           GError **error);
