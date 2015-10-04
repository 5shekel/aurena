/* GStreamer
 * Copyright (C) 2015 Jan Schmidt <jan@centricular.com>
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
#ifndef __AUR_RTSP_PLAY_MEDIA_H__
#define __AUR_RTSP_PLAY_MEDIA_H__

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <server/aur-server-types.h>

typedef struct _AurRTSPPlayMediaFactoryClass AurRTSPPlayMediaFactoryClass;

AurRTSPPlayMediaFactory * aur_rtsp_play_media_factory_new     (void);

#endif