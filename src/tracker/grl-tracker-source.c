/*
 * Copyright (C) 2011-2012 Igalia S.L.
 * Copyright (C) 2011 Intel Corporation.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
 *
 * Authors: Juan A. Suarez Romero <jasuarez@igalia.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <grilo.h>
#include <string.h>
#include <tracker-sparql.h>

#include "grl-tracker.h"
#include "grl-tracker-source.h"
#include "grl-tracker-source-priv.h"
#include "grl-tracker-source-api.h"
#include "grl-tracker-source-cache.h"
#include "grl-tracker-source-notif.h"
#include "grl-tracker-utils.h"

/* --------- Logging  -------- */

#define GRL_LOG_DOMAIN_DEFAULT tracker_source_log_domain
GRL_LOG_DOMAIN_STATIC(tracker_source_log_domain);

/* ------- Definitions ------- */

#define MEDIA_TYPE "grilo-media-type"

#define TRACKER_ITEM_CACHE_SIZE (10000)

/* --- Other --- */

enum {
  PROP_0,
  PROP_TRACKER_CONNECTION,
  PROP_TRACKER_DATASOURCE,
};

static void grl_tracker_source_set_property (GObject      *object,
                                             guint         propid,
                                             const GValue *value,
                                             GParamSpec   *pspec);

static void grl_tracker_source_finalize (GObject *object);

/* ===================== Globals  ================= */

/* shared data across  */
GrlTrackerCache *grl_tracker_item_cache;
GHashTable *grl_tracker_source_sources_modified;
GHashTable *grl_tracker_source_sources;

/* ================== TrackerSource GObject ================ */

G_DEFINE_TYPE (GrlTrackerSource, grl_tracker_source, GRL_TYPE_SOURCE);

static GrlTrackerSource *
grl_tracker_source_new (TrackerSparqlConnection *connection)
{
  GRL_DEBUG ("%s", __FUNCTION__);

  return g_object_new (GRL_TRACKER_SOURCE_TYPE,
                       "source-id", GRL_TRACKER_SOURCE_ID,
                       "source-name", GRL_TRACKER_SOURCE_NAME,
                       "source-desc", GRL_TRACKER_SOURCE_DESC,
                       "tracker-connection", connection,
                       "tracker-datasource", "",
                       NULL);
}

static void
grl_tracker_source_class_init (GrlTrackerSourceClass * klass)
{
  GObjectClass        *g_class      = G_OBJECT_CLASS (klass);
  GrlSourceClass      *source_class = GRL_SOURCE_CLASS (klass);

  g_class->finalize     = grl_tracker_source_finalize;
  g_class->set_property = grl_tracker_source_set_property;

  source_class->cancel              = grl_tracker_source_cancel;
  source_class->supported_keys      = grl_tracker_supported_keys;
  source_class->writable_keys       = grl_tracker_source_writable_keys;
  source_class->store_metadata      = grl_tracker_source_store_metadata;
  source_class->query               = grl_tracker_source_query;
  source_class->resolve             = grl_tracker_source_resolve;
  source_class->may_resolve         = grl_tracker_source_may_resolve;
  source_class->search              = grl_tracker_source_search;
  source_class->browse              = grl_tracker_source_browse;
  source_class->notify_change_start = grl_tracker_source_change_start;
  source_class->notify_change_stop  = grl_tracker_source_change_stop;
  source_class->supported_operations = grl_tracker_source_supported_operations;
  source_class->get_caps            = grl_tracker_source_get_caps;
  source_class->test_media_from_uri = grl_tracker_source_test_media_from_uri;
  source_class->media_from_uri      = grl_tracker_source_get_media_from_uri;

  g_object_class_install_property (g_class,
                                   PROP_TRACKER_CONNECTION,
                                   g_param_spec_object ("tracker-connection",
                                                        "tracker connection",
                                                        "A Tracker connection",
                                                        TRACKER_SPARQL_TYPE_CONNECTION,
                                                        G_PARAM_WRITABLE
                                                        | G_PARAM_CONSTRUCT_ONLY
                                                        | G_PARAM_STATIC_NAME));

 g_object_class_install_property (g_class,
                                  PROP_TRACKER_DATASOURCE,
                                  g_param_spec_string ("tracker-datasource",
                                                       "tracker datasource",
                                                       "A Tracker nie:DataSource URN",
                                                       NULL,
                                                       G_PARAM_WRITABLE
                                                       | G_PARAM_CONSTRUCT_ONLY
                                                       | G_PARAM_STATIC_NAME));

  g_type_class_add_private (klass, sizeof (GrlTrackerSourcePriv));
}

static void
grl_tracker_source_init (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv = GRL_TRACKER_SOURCE_GET_PRIVATE (source);

  source->priv = priv;

  priv->operations = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
grl_tracker_source_finalize (GObject *object)
{
  GrlTrackerSource *self;

  self = GRL_TRACKER_SOURCE (object);

  g_clear_object (&self->priv->tracker_connection);

  G_OBJECT_CLASS (grl_tracker_source_parent_class)->finalize (object);
}

static void
grl_tracker_source_set_property (GObject      *object,
                                 guint         propid,
                                 const GValue *value,
                                 GParamSpec   *pspec)

{
  GrlTrackerSourcePriv *priv = GRL_TRACKER_SOURCE_GET_PRIVATE (object);

  switch (propid) {
    case PROP_TRACKER_CONNECTION:
      g_clear_object (&priv->tracker_connection);
      priv->tracker_connection = g_object_ref (g_value_get_object (value));
      break;

    case PROP_TRACKER_DATASOURCE:
      g_clear_pointer (&priv->tracker_datasource, g_free);
      priv->tracker_datasource = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

const gchar *
grl_tracker_source_get_tracker_source (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv;

  g_return_val_if_fail (GRL_IS_TRACKER_SOURCE (source), NULL);

  priv = source->priv;

  return priv->tracker_datasource;
}

TrackerSparqlConnection *
grl_tracker_source_get_tracker_connection (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv;

  g_return_val_if_fail (GRL_IS_TRACKER_SOURCE (source), NULL);

  priv = source->priv;

  return priv->tracker_connection;
}

/* =================== TrackerSource Plugin  =============== */

void
grl_tracker_add_source (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv = GRL_TRACKER_SOURCE_GET_PRIVATE (source);

  GRL_DEBUG ("====================>add source '%s' count=%u",
             grl_source_get_name (GRL_SOURCE (source)),
             priv->notification_ref);

  if (priv->notification_ref > 0) {
    priv->notification_ref--;
  }
  if (priv->notification_ref == 0) {
    g_hash_table_remove (grl_tracker_source_sources_modified,
                         grl_tracker_source_get_tracker_source (source));
    g_hash_table_insert (grl_tracker_source_sources,
                         (gpointer) grl_tracker_source_get_tracker_source (source),
                         g_object_ref (source));
    priv->state = GRL_TRACKER_SOURCE_STATE_RUNNING;
    grl_registry_register_source (grl_registry_get_default (),
                                  grl_tracker_plugin,
                                  GRL_SOURCE (source),
                                  NULL);
  }
}

void
grl_tracker_del_source (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv = GRL_TRACKER_SOURCE_GET_PRIVATE (source);

  GRL_DEBUG ("==================>del source '%s' count=%u",
             grl_source_get_name (GRL_SOURCE (source)),
             priv->notification_ref);
  if (priv->notification_ref > 0) {
    priv->notification_ref--;
  }
  if (priv->notification_ref == 0) {
    g_hash_table_remove (grl_tracker_source_sources_modified,
                         grl_tracker_source_get_tracker_source (source));
    g_hash_table_remove (grl_tracker_source_sources,
                         grl_tracker_source_get_tracker_source (source));
    grl_tracker_source_cache_del_source (grl_tracker_item_cache, source);
    priv->state = GRL_TRACKER_SOURCE_STATE_DELETED;
    grl_registry_unregister_source (grl_registry_get_default (),
                                    GRL_SOURCE (source),
                                    NULL);
  }
}

gboolean
grl_tracker_source_can_notify (GrlTrackerSource *source)
{
  GrlTrackerSourcePriv *priv = GRL_TRACKER_SOURCE_GET_PRIVATE (source);

  if (priv->state == GRL_TRACKER_SOURCE_STATE_RUNNING)
    return priv->notify_changes;

  return FALSE;
}

GrlTrackerSource *
grl_tracker_source_find (const gchar *id)
{
  GrlTrackerSource *source;

  source = g_hash_table_lookup (grl_tracker_source_sources, id);

  if (source)
    return (GrlTrackerSource *) source;

  return
    (GrlTrackerSource *) g_hash_table_lookup (grl_tracker_source_sources_modified,
                                              id);
}

static gboolean
match_plugin_id (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
  if (g_strcmp0 (grl_source_get_id (GRL_SOURCE (value)),
                 (gchar *) user_data) == 0) {
    return TRUE;
  }

  return FALSE;
}

/* Search for registered plugin with @id */
GrlTrackerSource *
grl_tracker_source_find_source (const gchar *id)
{
  GrlTrackerSource *source;

  source = g_hash_table_find (grl_tracker_source_sources,
                              match_plugin_id,
                              (gpointer) id);
  if (source) {
    return source;
  }

  return g_hash_table_find (grl_tracker_source_sources_modified,
                            match_plugin_id,
                            (gpointer) id);
}

static void
tracker_get_datasource_cb (GObject             *object,
                           GAsyncResult        *result,
                           TrackerSparqlCursor *cursor)
{
  const gchar *type, *datasource, *datasource_name, *uri;
  gboolean source_available = FALSE;
  GError *error = NULL;
  GrlTrackerSource *source;

  GRL_DEBUG ("%s", __FUNCTION__);

  if (!tracker_sparql_cursor_next_finish (cursor, result, &error)) {
    if (error == NULL) {
      GRL_DEBUG ("\tEnd of parsing of devices");
    } else {
      GRL_WARNING ("\tError while parsing devices: %s", error->message);
      g_error_free (error);
    }
    g_object_unref (G_OBJECT (cursor));
    return;
  }

  type = tracker_sparql_cursor_get_string (cursor, 0, NULL);
  datasource = tracker_sparql_cursor_get_string (cursor, 1, NULL);
  datasource_name = tracker_sparql_cursor_get_string (cursor, 2, NULL);
  uri = tracker_sparql_cursor_get_string (cursor, 3, NULL);
  if (tracker_sparql_cursor_is_bound (cursor, 4))
    source_available = tracker_sparql_cursor_get_boolean (cursor, 4);

  source = grl_tracker_source_find (datasource);

  if ((source == NULL) && source_available) {
    gchar *source_name = grl_tracker_get_source_name (type, uri, datasource,
                                                      datasource_name);
    if (source_name) {
      GRL_DEBUG ("\tnew datasource: urn=%s name=%s uri=%s => name=%s\n",
                 datasource, datasource_name, uri, source_name);
      source = g_object_new (GRL_TRACKER_SOURCE_TYPE,
                             "source-id", datasource,
                             "source-name", source_name,
                             "source-desc", GRL_TRACKER_SOURCE_DESC,
                             "tracker-connection", grl_tracker_connection,
                             "tracker-datasource", datasource,
                             NULL);
      grl_tracker_add_source (source);
      g_object_unref (source);
      g_free (source_name);
    }
  }

  tracker_sparql_cursor_next_async (cursor, NULL,
                                    (GAsyncReadyCallback) tracker_get_datasource_cb,
                                    cursor);
}

static void
tracker_get_datasources_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      data)
{
  GError *error = NULL;
  TrackerSparqlCursor *cursor;

  GRL_DEBUG ("%s", __FUNCTION__);

  cursor = tracker_sparql_connection_query_finish (grl_tracker_connection,
                                                   result, &error);

  if (error) {
    GRL_WARNING ("Cannot handle datasource request : %s", error->message);
    g_error_free (error);
    return;
  }

  tracker_sparql_cursor_next_async (cursor, NULL,
                                    (GAsyncReadyCallback) tracker_get_datasource_cb,
                                    cursor);
}

void
grl_tracker_source_sources_init (void)
{

  GRL_LOG_DOMAIN_INIT (tracker_source_log_domain, "tracker-source");

  GRL_DEBUG ("%s", __FUNCTION__);

  grl_tracker_item_cache =
    grl_tracker_source_cache_new (TRACKER_ITEM_CACHE_SIZE);
  grl_tracker_source_sources = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      NULL, g_object_unref);
  grl_tracker_source_sources_modified = g_hash_table_new_full (g_str_hash,
                                                               g_str_equal,
                                                               NULL,
                                                               g_object_unref);


  if (grl_tracker_connection != NULL) {
    grl_tracker_source_dbus_start_watch ();

    if (grl_tracker_per_device_source == TRUE) {
      /* Let's discover available data sources. */
      GRL_DEBUG ("\tper device source mode request: '"
                 TRACKER_DATASOURCES_REQUEST "'");

      tracker_sparql_connection_query_async (grl_tracker_connection,
                                             TRACKER_DATASOURCES_REQUEST,
                                             NULL,
                                             (GAsyncReadyCallback) tracker_get_datasources_cb,
                                             NULL);
    } else {
      GrlTrackerSource *source;

      /* One source to rule them all. */
      source = grl_tracker_source_new (grl_tracker_connection);
      grl_tracker_add_source (source);
      g_object_unref (source);
    }
  }
}
