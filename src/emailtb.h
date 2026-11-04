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
 * Authors:
 *       Matthias Clasen <mclasen@redhat.com>
 */

#pragma once

#include <gio/gio.h>

gboolean compose_mail_thunderbird (GAppInfo           *info,
                                   const char * const *addrs,
                                   const char * const *cc,
                                   const char * const *bcc,
                                   const char         *subject,
                                   const char         *body,
                                   const char        **attachments,
                                   GError            **error);

gboolean handle_compose_email_tb  (XdpImplEmail          *object,
                                   GDBusMethodInvocation *invocation,
                                   const char            *arg_handle,
                                   const char            *arg_app_id,
                                   const char            *arg_parent_window,
                                   GVariant              *arg_options,
                                   const char * const    *addrs,
                                   const char * const    *cc,
                                   const char * const    *bcc,
                                   const char            *subject,
                                   const char            *body,
                                   const char           **attachments);
