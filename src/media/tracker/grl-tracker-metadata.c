/*
 * Copyright (C) 2011 Intel Corporation.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
 *
 * Authors: Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <tracker-sparql.h>

#include "grl-tracker-metadata.h"
#include "grl-tracker-utils.h"

/* --------- Logging  -------- */

#define GRL_LOG_DOMAIN_DEFAULT tracker_metadata_request_log_domain

GRL_LOG_DOMAIN_STATIC(tracker_metadata_request_log_domain);
GRL_LOG_DOMAIN_STATIC(tracker_metadata_result_log_domain);

/* Inputs/requests */
#define GRL_IDEBUG(args...)                     \
  GRL_LOG (tracker_metadata_request_log_domain, \
           GRL_LOG_LEVEL_DEBUG, args)

/* Outputs/results */
#define GRL_ODEBUG(args...)                     \
  GRL_LOG (tracker_metadata_result_log_domain,  \
           GRL_LOG_LEVEL_DEBUG, args)

/* ------- Definitions ------- */

#define TRACKER_RESOLVE_REQUEST                 \
  "SELECT %s "                                  \
  "WHERE "                                      \
  "{ "                                          \
  "?urn a nie:DataObject . "                    \
  "?urn nie:url \"%s\" " \
  "}"

/**/

#define GRL_TRACKER_METADATA_GET_PRIVATE(object)		\
  (G_TYPE_INSTANCE_GET_PRIVATE((object),                        \
                               GRL_TRACKER_METADATA_TYPE,       \
                               GrlTrackerMetadataPriv))

enum {
  PROP_0,
  PROP_TRACKER_CONNECTION,
};

struct _GrlTrackerMetadataPriv {
  TrackerSparqlConnection *tracker_connection;
};

static void grl_tracker_metadata_set_property (GObject      *object,
                                               guint         propid,
                                               const GValue *value,
                                               GParamSpec   *pspec);

static void grl_tracker_metadata_finalize (GObject *object);

static gboolean grl_tracker_metadata_may_resolve (GrlMetadataSource  *source,
                                                  GrlMedia           *media,
                                                  GrlKeyID            key_id,
                                                  GList             **missing_keys);

static void grl_tracker_metadata_resolve (GrlMetadataSource            *source,
                                          GrlMetadataSourceResolveSpec *rs);

/* ================== TrackerMetadata GObject ================ */

G_DEFINE_TYPE (GrlTrackerMetadata, grl_tracker_metadata, GRL_TYPE_METADATA_SOURCE);

static GrlTrackerMetadata *
grl_tracker_metadata_new (TrackerSparqlConnection *connection)
{
  GRL_DEBUG ("%s", __FUNCTION__);

  return g_object_new (GRL_TRACKER_METADATA_TYPE,
                       "source-id", GRL_TRACKER_METADATA_ID,
                       "source-name", GRL_TRACKER_METADATA_NAME,
                       "source-desc", GRL_TRACKER_METADATA_DESC,
                       "tracker-connection", connection,
                       NULL);
}

static void
grl_tracker_metadata_class_init (GrlTrackerMetadataClass * klass)
{
  GrlMetadataSourceClass *metadata_class = GRL_METADATA_SOURCE_CLASS (klass);
  GObjectClass           *g_class        = G_OBJECT_CLASS (klass);

  metadata_class->supported_keys = grl_tracker_supported_keys;
  metadata_class->may_resolve    = grl_tracker_metadata_may_resolve;
  metadata_class->resolve        = grl_tracker_metadata_resolve;

  g_class->finalize     = grl_tracker_metadata_finalize;
  g_class->set_property = grl_tracker_metadata_set_property;

  g_object_class_install_property (g_class,
                                   PROP_TRACKER_CONNECTION,
                                   g_param_spec_object ("tracker-connection",
                                                        "tracker connection",
                                                        "A Tracker connection",
                                                        TRACKER_SPARQL_TYPE_CONNECTION,
                                                        G_PARAM_WRITABLE
                                                        | G_PARAM_CONSTRUCT_ONLY
                                                        | G_PARAM_STATIC_NAME));

  g_type_class_add_private (klass, sizeof (GrlTrackerMetadataPriv));
}

static void
grl_tracker_metadata_init (GrlTrackerMetadata *source)
{
  GrlTrackerMetadataPriv *priv = GRL_TRACKER_METADATA_GET_PRIVATE (source);

  source->priv = priv;
}

static void
grl_tracker_metadata_finalize (GObject *object)
{
  GrlTrackerMetadata *self;

  self = GRL_TRACKER_METADATA (object);
  if (self->priv->tracker_connection)
    g_object_unref (self->priv->tracker_connection);

  G_OBJECT_CLASS (grl_tracker_metadata_parent_class)->finalize (object);
}

static void
grl_tracker_metadata_set_property (GObject      *object,
                                   guint         propid,
                                   const GValue *value,
                                   GParamSpec   *pspec)

{
  GrlTrackerMetadataPriv *priv = GRL_TRACKER_METADATA_GET_PRIVATE (object);

  switch (propid) {
    case PROP_TRACKER_CONNECTION:
      if (priv->tracker_connection != NULL)
        g_object_unref (G_OBJECT (priv->tracker_connection));
      priv->tracker_connection = g_object_ref (g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**/

static void
fill_grilo_media_from_sparql (GrlTrackerMetadata  *source,
                              GrlMedia            *media,
                              TrackerSparqlCursor *cursor,
                              gint                 column)
{
  const gchar *sparql_key = tracker_sparql_cursor_get_variable_name (cursor,
                                                                     column);
  tracker_grl_sparql_t *assoc =
    grl_tracker_get_mapping_from_sparql (sparql_key);
  union {
    gint int_val;
    gdouble double_val;
    const gchar *str_val;
  } val;

  if (assoc == NULL)
    return;

  GRL_ODEBUG ("\tSetting media prop (col=%i/var=%s/prop=%s) %s",
              column,
              sparql_key,
              g_param_spec_get_name (G_PARAM_SPEC (assoc->grl_key)),
              tracker_sparql_cursor_get_string (cursor, column, NULL));

  if (tracker_sparql_cursor_is_bound (cursor, column) == FALSE) {
    GRL_ODEBUG ("\t\tDropping, no data");
    return;
  }

  if (grl_data_key_is_known (GRL_DATA (media), assoc->grl_key)) {
    GRL_ODEBUG ("\t\tDropping, already here");
    return;
  }

  switch (G_PARAM_SPEC (assoc->grl_key)->value_type) {
  case G_TYPE_STRING:
    val.str_val = tracker_sparql_cursor_get_string (cursor, column, NULL);
    if (val.str_val != NULL)
      grl_data_set_string (GRL_DATA (media), assoc->grl_key, val.str_val);
    break;

  case G_TYPE_INT:
    val.int_val = tracker_sparql_cursor_get_integer (cursor, column);
    grl_data_set_int (GRL_DATA (media), assoc->grl_key, val.int_val);
    break;

  case G_TYPE_FLOAT:
    val.double_val = tracker_sparql_cursor_get_double (cursor, column);
    grl_data_set_float (GRL_DATA (media), assoc->grl_key, (gfloat) val.double_val);
    break;

  default:
    GRL_ODEBUG ("\t\tUnexpected data type");
    break;
  }
}

static void
tracker_resolve_cb (GObject                      *source_object,
                    GAsyncResult                 *result,
                    GrlMetadataSourceResolveSpec *rs)
{
  GrlTrackerMetadataPriv *priv = GRL_TRACKER_METADATA_GET_PRIVATE (rs->source);
  gint                  col;
  GError               *tracker_error = NULL, *error = NULL;
  TrackerSparqlCursor  *cursor;

  GRL_ODEBUG ("%s", __FUNCTION__);

  cursor = tracker_sparql_connection_query_finish (priv->tracker_connection,
                                                   result, &tracker_error);

  if (tracker_error) {
    GRL_WARNING ("Could not execute sparql resolve query : %s",
                 tracker_error->message);

    error = g_error_new (GRL_CORE_ERROR,
			 GRL_CORE_ERROR_BROWSE_FAILED,
			 "Failed to start resolve action : %s",
                         tracker_error->message);

    rs->callback (rs->source, NULL, rs->user_data, error);

    g_error_free (tracker_error);
    g_error_free (error);

    goto end_operation;
  }


  if (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
    /* Translate Sparql result into Grilo result */
    for (col = 0 ; col < tracker_sparql_cursor_get_n_columns (cursor) ; col++) {
      fill_grilo_media_from_sparql (GRL_TRACKER_METADATA (rs->source),
                                    rs->media, cursor, col);
    }

    rs->callback (rs->source, rs->media, rs->user_data, NULL);
  }

 end_operation:
  if (cursor)
    g_object_unref (G_OBJECT (cursor));
}


/**/

static gboolean
grl_tracker_metadata_may_resolve (GrlMetadataSource  *source,
                                  GrlMedia           *media,
                                  GrlKeyID            key_id,
                                  GList             **missing_keys)
{
  const gchar *url;

  if (media) {
    url = grl_media_get_url (media);

    if (url && grl_tracker_key_is_supported (key_id)) {
      return TRUE;
    }
  } else {
    if (grl_tracker_key_is_supported (key_id))
      return TRUE;
  }

  return FALSE;
}


static void
grl_tracker_metadata_resolve (GrlMetadataSource            *source,
                              GrlMetadataSourceResolveSpec *rs)
{
  GrlTrackerMetadataPriv *priv = GRL_TRACKER_METADATA_GET_PRIVATE (source);
  const gchar *url = grl_media_get_url (rs->media);
  gchar *sparql_select, *sparql_final;

  GRL_IDEBUG ("%s", __FUNCTION__);

  g_return_if_fail (url != NULL);

  sparql_select = grl_tracker_media_get_select_string (rs->keys);
  sparql_final = g_strdup_printf (TRACKER_RESOLVE_REQUEST, sparql_select, url);

  tracker_sparql_connection_query_async (priv->tracker_connection,
                                         sparql_final,
                                         NULL,
                                         (GAsyncReadyCallback) tracker_resolve_cb,
                                         rs);

  GRL_IDEBUG ("request: '%s'", sparql_final);

  g_free (sparql_select);
  g_free (sparql_final);
}

/* =================== TrackerMedia Plugin  =============== */

void
grl_tracker_metadata_source_init (void)
{
  GrlTrackerMetadata *source = grl_tracker_metadata_new (grl_tracker_connection);

  grl_plugin_registry_register_source (grl_plugin_registry_get_default (),
                                       grl_tracker_plugin,
                                       GRL_MEDIA_PLUGIN (source),
                                       NULL);
}

void
grl_tracker_metadata_init_requests (void)
{
  GRL_LOG_DOMAIN_INIT (tracker_metadata_request_log_domain,
                       "tracker-metadata-request");
  GRL_LOG_DOMAIN_INIT (tracker_metadata_result_log_domain,
                       "tracker-metadata-result");
}
