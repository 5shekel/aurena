/* GStreamer
 * Copyright (C) 2012-2015 Jan Schmidt <jan@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libsoup/soup-server.h>
#include <string.h>

#include "aur-http-client.h"

G_DEFINE_TYPE (AurHTTPClient, aur_http_client, AUR_TYPE_WEBSOCKET_PARSER);

enum
{
  CONNECTION_LOST,
  LAST_SIGNAL
};

static guint aur_http_client_signals[LAST_SIGNAL] = { 0 };

typedef struct _PendingMsg PendingMsg;

struct _PendingMsg
{
  gchar *body;
  gsize len;
};

static guint next_conn_id = 1;

static void aur_http_client_finalize (GObject * object);
static void aur_http_client_dispose (GObject * object);

static void
aur_http_client_init (AurHTTPClient * client)
{
  client->conn_id = next_conn_id++;
}

static void
aur_http_client_class_init (AurHTTPClientClass * client_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (client_class);

  gobject_class->dispose = aur_http_client_dispose;
  gobject_class->finalize = aur_http_client_finalize;

  aur_http_client_signals[CONNECTION_LOST] =
      g_signal_new ("connection-lost", G_TYPE_FROM_CLASS (client_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
}

static void
aur_http_client_finalize (GObject * object)
{
  AurHTTPClient *client = (AurHTTPClient *) (object);

  if (client->disco_sig) {
    g_signal_handler_disconnect (client->event_pipe, client->disco_sig);
    client->disco_sig = 0;
  }
  if (client->net_event_sig) {
    g_signal_handler_disconnect (client->event_pipe, client->net_event_sig);
    client->net_event_sig = 0;
  }
  if (client->wrote_info_sig) {
    g_signal_handler_disconnect (client->event_pipe, client->wrote_info_sig);
    client->wrote_info_sig = 0;
  }

  if (client->need_body_complete) {
    soup_message_body_complete (client->event_pipe->response_body);
  }

  if (client->socket)
    g_socket_shutdown (client->socket, TRUE, TRUE, NULL);

  g_free (client->host);

  g_free (client->out_buf);

  G_OBJECT_CLASS (aur_http_client_parent_class)->finalize (object);
}

static void
aur_http_client_dispose (GObject * object)
{
  AurHTTPClient *client = (AurHTTPClient *) (object);

  if (client->io) {
    g_source_remove (client->io_watch);
    g_io_channel_shutdown (client->io, TRUE, NULL);
    g_io_channel_unref (client->io);
    client->io = NULL;
  }

  G_OBJECT_CLASS (aur_http_client_parent_class)->dispose (object);
}

static void
aur_server_connection_lost (AurHTTPClient * client)
{
  if (client->fired_conn_lost)
    return;
  client->fired_conn_lost = TRUE;

  if (client->io) {
    g_source_remove (client->io_watch);
    g_io_channel_shutdown (client->io, TRUE, NULL);
    g_io_channel_unref (client->io);
    client->io = NULL;
  }

  if (client->type == AUR_HTTP_CLIENT_CHUNKED ||
      client->type == AUR_HTTP_CLIENT_SINGLE) {
    if (client->need_body_complete) {
      soup_message_body_complete (client->event_pipe->response_body);
      client->need_body_complete = FALSE;
      soup_server_unpause_message (client->soup, client->event_pipe);
    }
  }

  if (client->socket) {
    g_socket_shutdown (client->socket, TRUE, TRUE, NULL);
    client->socket = NULL;
  }

  g_signal_emit (client, aur_http_client_signals[CONNECTION_LOST], 0, NULL);
}

static void
aur_http_client_disconnect (G_GNUC_UNUSED SoupMessage * message,
    AurHTTPClient * client)
{
  client->need_body_complete = FALSE;
  aur_server_connection_lost (client);
}

static void
aur_http_client_network_event (G_GNUC_UNUSED SoupMessage * msg,
    G_GNUC_UNUSED GSocketClientEvent event,
    G_GNUC_UNUSED GIOStream * connection, G_GNUC_UNUSED AurHTTPClient * client)
{
  g_print ("client %u network event %d\n", client->conn_id, event);
}

/* Callbacks used for websocket clients */

static gboolean
aur_http_client_io_cb (G_GNUC_UNUSED GIOChannel * source,
    GIOCondition condition, AurHTTPClient * client)
{
  GIOStatus status = G_IO_STATUS_NORMAL;
#if 0
  g_print ("Got IO callback for client %p w/ condition %u\n", client,
      (guint) (condition));
#endif

  if (condition & (G_IO_HUP | G_IO_ERR)) {
    aur_server_connection_lost (client);
    return FALSE;
  }

  if (condition & G_IO_IN) {
    status =
        aur_websocket_parser_read_io (AUR_WEBSOCKET_PARSER (client),
        client->io);
  }

  if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
    // no more data
    aur_server_connection_lost (client);
    return FALSE;
  }

  return TRUE;
}

static void
aur_http_client_wrote_headers (SoupMessage * msg, AurHTTPClient * client)
{
  GList *cur;

  /* Pause the message so Soup doesn't do any more responding */
  soup_server_pause_message (client->soup, msg);

  g_print ("client %u ready for traffic\n", client->conn_id);

  client->io = g_io_channel_unix_new (g_socket_get_fd (client->socket));
  g_io_channel_set_encoding (client->io, NULL, NULL);
  g_io_channel_set_buffered (client->io, FALSE);
  client->io_watch = g_io_add_watch (client->io, G_IO_IN | G_IO_HUP,
      (GIOFunc) (aur_http_client_io_cb), client);

  /* Send any pending messages */
  while ((cur = client->pending_msgs)) {
    GList *next;
    PendingMsg *msg = (PendingMsg *) (cur->data);

    next = g_list_next (cur);

    aur_http_client_send_message (client, msg->body, msg->len);
    client->pending_msgs = g_list_delete_link (client->pending_msgs, cur);
    g_free (msg->body);
    g_free (msg);

    cur = next;
  }
}

static gchar *
calc_websocket_challenge_reply (const gchar * key)
{
  const gchar *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  gchar *ret = NULL;
  gchar *concat = g_strconcat (key, guid, NULL);
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);

  guint8 sha1[20];
  gsize len = 20;

  //g_print ("challenge: %s\n", key);

  g_checksum_update (checksum, (guchar *) (concat), -1);
  g_checksum_get_digest (checksum, sha1, &len);

  g_free (concat);

  ret = g_base64_encode (sha1, len);

  g_checksum_free (checksum);
  //g_print ("reply: %s\n", ret);

  return ret;
}

static gboolean
http_list_contains_value (const gchar * val, const gchar * needle)
{
  /* Connection params list must request upgrade to websocket */
  gchar **tmp = g_strsplit (val, ",", 0);
  gchar **cur;
  gboolean found_needle = FALSE;

  for (cur = tmp; *cur != NULL; cur++) {
    g_strstrip (*cur);
    if (g_ascii_strcasecmp (*cur, needle) == 0) {
      found_needle = TRUE;
      break;
    }
  }
  g_strfreev (tmp);
  return found_needle;
}

static gboolean
is_websocket_client (AurHTTPClient * client)
{
  /* Check for request headers. Example:
   * Upgrade: websocket
   * Connection: Upgrade, Keep-Alive
   * Sec-WebSocket-Key: XYZABC123
   * Sec-WebSocket-Protocol: aurena
   * Sec-WebSocket-Version: 13
   */
  SoupMessage *msg = client->event_pipe;
  SoupMessageHeaders *req_hdrs = msg->request_headers;
  const gchar *val;
  gint protocol_ver = 0;

  if ((val = soup_message_headers_get_one (req_hdrs, "Upgrade")) == NULL)
    return FALSE;
  if (g_ascii_strcasecmp (val, "websocket") != 0)
    return FALSE;
  if ((val = soup_message_headers_get_list (req_hdrs, "Connection")) == NULL)
    return FALSE;

  /* Connection params list must request upgrade to websocket */
  if (!http_list_contains_value (val, "upgrade"))
    return FALSE;

  if ((val =
          soup_message_headers_get_one (req_hdrs, "Sec-WebSocket-Key")) == NULL)
    return FALSE;
  if ((val =
          soup_message_headers_get_list (req_hdrs,
              "Sec-WebSocket-Protocol")) == NULL)
    return FALSE;

  if (!http_list_contains_value (val, "aurena"))
    return FALSE;

  /* Requested protocol version must be 13 or 8 */
  if ((val = soup_message_headers_get_list (req_hdrs,
              "Sec-WebSocket-Version")) == NULL)
    return FALSE;

  if (http_list_contains_value (val, "13"))
    protocol_ver = 13;
  else if (http_list_contains_value (val, "8"))
    protocol_ver = 8;

  if (protocol_ver == 0)
    return FALSE;               /* No supported version found */

  g_print ("WebSocket connection with protocol %d\n", protocol_ver);
  client->websocket_protocol = protocol_ver;

  return TRUE;
}

AurHTTPClient *
aur_http_client_new (SoupServer * soup, SoupMessage * msg,
    SoupClientContext * context)
{
  AurHTTPClient *client = g_object_new (AUR_TYPE_HTTP_CLIENT, NULL);
  const gchar *accept_challenge;
  gchar *accept_reply;
  const gchar *client_name;

  client_name = soup_message_headers_get_one (msg->request_headers, "Client-Name");

  client->soup = soup;
  client->event_pipe = msg;
  if (client_name)
    client->host = g_strdup (client_name);
  else
    client->host = g_strdup (soup_client_context_get_host (context));

  client->net_event_sig = g_signal_connect (msg, "network-event",
      G_CALLBACK (aur_http_client_network_event), client);
  client->disco_sig = g_signal_connect (msg, "finished",
      G_CALLBACK (aur_http_client_disconnect), client);

  if (!is_websocket_client (client)) {
    client->type = AUR_HTTP_CLIENT_CHUNKED;
    client->need_body_complete = TRUE;

    soup_message_headers_set_encoding (msg->response_headers,
        SOUP_ENCODING_CHUNKED);
    soup_message_set_status (msg, SOUP_STATUS_OK);
    return client;
  }

  /* Otherwise, it's a websocket client */
  client->type = AUR_HTTP_CLIENT_WEBSOCKET;
  client->need_body_complete = FALSE;

  client->socket = soup_client_context_get_gsocket (context);
  /* FIXME: Steal connection on newer libsoup */

  accept_challenge =
      soup_message_headers_get_one (msg->request_headers, "Sec-WebSocket-Key");
  accept_reply = calc_websocket_challenge_reply (accept_challenge);

  soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_EOF);

  soup_message_set_status (msg, SOUP_STATUS_SWITCHING_PROTOCOLS);
  soup_message_headers_replace (msg->response_headers, "Upgrade", "websocket");
  soup_message_headers_replace (msg->response_headers, "Connection", "Upgrade");
  soup_message_headers_replace (msg->response_headers, "Sec-WebSocket-Accept",
      accept_reply);
  soup_message_headers_replace (msg->response_headers, "Sec-WebSocket-Protocol",
      "aurena");

  g_free (accept_reply);

  client->wrote_info_sig = g_signal_connect (msg, "wrote-informational",
      G_CALLBACK (aur_http_client_wrote_headers), client);

  return client;
}

/* Send a single message to the client and
 * then close the connection */
AurHTTPClient *
aur_http_client_new_single (SoupServer * soup, SoupMessage * msg,
    G_GNUC_UNUSED SoupClientContext * context)
{
  AurHTTPClient *client = g_object_new (AUR_TYPE_HTTP_CLIENT, NULL);

  client->soup = soup;
  client->event_pipe = msg;
  client->host = g_strdup (soup_client_context_get_host (context));

  client->net_event_sig = g_signal_connect (msg, "network-event",
      G_CALLBACK (aur_http_client_network_event), client);
  client->disco_sig = g_signal_connect (msg, "finished",
      G_CALLBACK (aur_http_client_disconnect), client);

  client->type = AUR_HTTP_CLIENT_SINGLE;
  client->need_body_complete = TRUE;

  soup_message_set_status (msg, SOUP_STATUS_OK);
  soup_server_pause_message (client->soup, msg);

  return client;
}

static GIOStatus
write_to_io_channel (GIOChannel * io, const gchar * buf, gsize len)
{
  GIOStatus status;
  gsize tot_written = 0;

  do {
    gsize written = 0;
    status = g_io_channel_write_chars (io, buf, len, &written, NULL);

    if (status == G_IO_STATUS_ERROR || status == G_IO_STATUS_EOF)
      return status;

    tot_written += written;
    buf += written;
  } while (tot_written < len && status == G_IO_STATUS_AGAIN);

  return G_IO_STATUS_NORMAL;
}

static void
write_fragment (AurHTTPClient * client, gchar * body, gsize len)
{
  gchar header[14];
  gsize header_len, i;
  const guint8 masking = 0x00;  // 0x80 to enable masking
  GIOStatus status;

  union
  {
    gchar bytes[4];
    guint32 val;
  } mask;

  header[0] = 0x81;
  header_len = 2;
  if (len < 126) {
    header[1] = masking | len;
  } else if (len < 65536) {
    header[1] = masking | 126;
    GST_WRITE_UINT16_BE (header + 2, (guint16) (len));
    header_len += 2;
  } else {
    header[1] = masking | 127;
    GST_WRITE_UINT64_BE (header + 2, (guint64) (len));
    header_len += 8;
  }
  /* Add a random mask word. */
  if (masking) {
    mask.val = g_random_int ();
    memcpy (header + header_len, mask.bytes, 4);
    header_len += 4;
  }

  /* Write WebSocket frame header */
  status = write_to_io_channel (client->io, header, header_len);
  if (status != G_IO_STATUS_NORMAL)
    goto done;

  /* Mask the body, and send it */
  if (masking) {
    if (len > client->out_bufsize) {
      client->out_bufsize = len;
      client->out_buf = g_realloc (client->out_buf, client->out_bufsize);
    }
    for (i = 0; i < len; i++) {
      client->out_buf[i] = body[i] ^ mask.bytes[i % 4];
    }
    status = write_to_io_channel (client->io, client->out_buf, len);
  } else {
    status = write_to_io_channel (client->io, body, len);
  }

done:
  if (status != G_IO_STATUS_NORMAL)
    aur_server_connection_lost (client);
}

void
aur_http_client_send_message (AurHTTPClient * client, gchar * body, gsize len)
{
  PendingMsg *msg;

  if (client->fired_conn_lost)
    return;

  if (client->type == AUR_HTTP_CLIENT_CHUNKED) {
    soup_message_body_append (client->event_pipe->response_body,
        SOUP_MEMORY_COPY, body, len);
    /* Add a NULL seperator, enable HTTP stack that abstract HTTP chunk. */
    soup_message_body_append (client->event_pipe->response_body,
        SOUP_MEMORY_COPY, "\0", 1);
    soup_server_unpause_message (client->soup, client->event_pipe);
    return;
  }
  if (client->type == AUR_HTTP_CLIENT_SINGLE) {
    soup_message_set_response (client->event_pipe,
        "application/json", SOUP_MEMORY_COPY, body, len);
    aur_server_connection_lost (client);
    return;
  }

  /* else, websocket connection */
  if (client->io) {
    write_fragment (client, body, len);
    return;
  }

  /* Websocket isn't ready to send yet. Store as a pending message */
  msg = g_new (PendingMsg, 1);
  msg->len = len;
  msg->body = g_memdup (body, len);

  client->pending_msgs = g_list_append (client->pending_msgs, msg);
}

const gchar *
aur_http_client_get_host (AurHTTPClient * client)
{
  return client->host;
}
