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

#ifndef __AUR_JSON_H__
#define __AUR_JSON_H__

#include <gst/gst.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

GstStructure *aur_json_to_gst_structure (JsonNode *root);
JsonNode *aur_json_from_gst_structure (const GstStructure *s);
gboolean aur_json_structure_get_int (const GstStructure *structure,
    const gchar *fieldname, gint *value);
gboolean aur_json_structure_get_int64 (const GstStructure *structure,
    const gchar *fieldname, gint64 *value);
gboolean aur_json_structure_get_double (const GstStructure *structure,
    const gchar *fieldname, gdouble *value);
gboolean aur_json_structure_get_boolean (const GstStructure *structure,
    const gchar *fieldname, gboolean *value);

G_END_DECLS

#endif
