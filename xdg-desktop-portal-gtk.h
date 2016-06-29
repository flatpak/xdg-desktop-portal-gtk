/*
 * Copyright © 2016 Red Hat, Inc
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
 *       Matthias Clasen <mclasen@redhat.com>
 */

#pragma once

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
