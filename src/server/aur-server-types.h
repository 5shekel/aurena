/* GStreamer
 * Copyright (C) 2012-2014 Jan Schmidt <thaytan@noraisin.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __AUR_SERVER_TYPES_H__
#define __AUR_SERVER_TYPES_H__

#include <glib.h>
#include <src/common/aur-types.h>

G_BEGIN_DECLS

typedef struct _AurAvahi AurAvahi;
typedef struct _AurClient AurClient;
typedef struct _AurHttpResource AurHttpResource;
typedef struct _AurManager AurManager;
typedef struct _AurMediaDB AurMediaDB;
typedef struct _AurServer AurServer;
typedef struct _AurServerClient AurServerClient;

G_END_DECLS

#endif
