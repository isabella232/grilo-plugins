/*
 * Copyright (C) 2011 Igalia S.L.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "grl-tracker-utils.h"
#include <glib/gi18n-lib.h>

/**/

static GHashTable *grl_to_sparql_mapping = NULL;
static GHashTable *sparql_to_grl_mapping = NULL;

GrlKeyID grl_metadata_key_tracker_urn;
GrlKeyID grl_metadata_key_gibest_hash;


/**/

static gchar *
build_flavored_key (gchar *key, const gchar *flavor)
{
  gint i = 0;

  while (key[i] != '\0') {
    if (!g_ascii_isalnum (key[i])) {
      key[i] = '_';
     }
    i++;
  }

  return g_strdup_printf ("%s_%s", key, flavor);
}

static void
set_orientation (TrackerSparqlCursor *cursor,
                 gint                 column,
                 GrlMedia            *media,
                 GrlKeyID             key)
{
  const gchar *str = tracker_sparql_cursor_get_string (cursor, column, NULL);

  if (g_str_has_suffix (str, "nfo#orientation-top"))
    grl_data_set_int (GRL_DATA (media), key, 0);
  else if (g_str_has_suffix (str, "nfo#orientation-right"))
    grl_data_set_int (GRL_DATA (media), key, 90);
  else if (g_str_has_suffix (str, "nfo#orientation-bottom"))
    grl_data_set_int (GRL_DATA (media), key, 180);
  else if (g_str_has_suffix (str, "nfo#orientation-left"))
    grl_data_set_int (GRL_DATA (media), key, 270);
}

static void
set_date (TrackerSparqlCursor *cursor,
          gint                 column,
          GrlMedia            *media,
          GrlKeyID             key)
{
  const gchar *str = tracker_sparql_cursor_get_string (cursor, column, NULL);
  if (key == GRL_METADATA_KEY_CREATION_DATE
      || key == GRL_METADATA_KEY_LAST_PLAYED
      || key == GRL_METADATA_KEY_MODIFICATION_DATE) {
    GDateTime *date = grl_date_time_from_iso8601 (str);
    if (date) {
      grl_data_set_boxed (GRL_DATA (media), key, date);
      g_date_time_unref (date);
    }
  }
}

static void
set_favourite (TrackerSparqlCursor *cursor,
               gint                 column,
               GrlMedia            *media,
               GrlKeyID             key)
{
  const gchar *str = tracker_sparql_cursor_get_string (cursor, column, NULL);
  gboolean is_favourite = FALSE;

  if (str != NULL && g_str_has_suffix (str, "predefined-tag-favorite"))
    is_favourite = TRUE;

  grl_data_set_boolean (GRL_DATA (media), key, is_favourite);
}

static void
set_title (TrackerSparqlCursor *cursor,
           gint                 column,
           GrlMedia            *media,
           GrlKeyID             key)
{
  const gchar *str = tracker_sparql_cursor_get_string (cursor, column, NULL);
  grl_data_set_boolean (GRL_DATA (media), GRL_METADATA_KEY_TITLE_FROM_FILENAME, FALSE);
  grl_media_set_title (media, str);
}

static void
set_string_metadata_keys (TrackerSparqlCursor *cursor,
                          gint                 column,
                          GrlMedia            *media,
                          GrlKeyID             key)
{
  const gchar *str = tracker_sparql_cursor_get_string (cursor, column, NULL);
  grl_data_set_string (GRL_DATA (media), key, str);
}

static void
set_int_metadata_keys (TrackerSparqlCursor *cursor,
                       gint                 column,
                       GrlMedia            *media,
                       GrlKeyID             key)
{
  const gint64 value = tracker_sparql_cursor_get_integer (cursor, column);
  grl_data_set_int (GRL_DATA (media), key, value);
}

static tracker_grl_sparql_t *
insert_key_mapping (GrlKeyID       grl_key,
                    const gchar   *sparql_var_name,
                    const gchar   *sparql_key_attr,
                    const gchar   *sparql_key_attr_call,
                    const gchar   *sparql_key_flavor,
                    GrlTypeFilter  filter)
{
  tracker_grl_sparql_t *assoc;
  GList *assoc_list;
  gchar *canon_name;

  g_return_val_if_fail (grl_key != GRL_METADATA_KEY_INVALID, NULL);

  assoc = g_new0 (tracker_grl_sparql_t, 1);
  assoc_list = g_hash_table_lookup (grl_to_sparql_mapping,
                                    GRLKEYID_TO_POINTER (grl_key));
  canon_name = g_strdup (GRL_METADATA_KEY_GET_NAME (grl_key));

  assoc->grl_key               = grl_key;
  assoc->sparql_var_name       = sparql_var_name;
  assoc->sparql_key_name       = build_flavored_key (canon_name,
                                                     sparql_key_flavor);
  assoc->sparql_key_name_canon = g_strdup (canon_name);
  assoc->sparql_key_attr       = sparql_key_attr;
  assoc->sparql_key_attr_call  = sparql_key_attr_call;
  assoc->sparql_key_flavor     = sparql_key_flavor;
  assoc->filter                = filter;

  assoc_list = g_list_append (assoc_list, assoc);

  g_hash_table_insert (grl_to_sparql_mapping,
                       GRLKEYID_TO_POINTER (grl_key),
                       assoc_list);
  g_hash_table_insert (sparql_to_grl_mapping,
                       (gpointer) assoc->sparql_key_name,
                       assoc);
  g_hash_table_insert (sparql_to_grl_mapping,
                       (gpointer) GRL_METADATA_KEY_GET_NAME (grl_key),
                       assoc);

  /* Grilo maps key names to SPARQL variables. Key names can contain dashes,
   * however SPARQL does not allow dashes in variable names. So use the to
   * underscores converted canon_name as additional mapping.
   */
  if (g_strrstr (assoc->sparql_key_name_canon, "_")) {
    g_hash_table_insert (sparql_to_grl_mapping,
                         (gpointer) assoc->sparql_key_name_canon,
                         assoc);
  }

  g_free (canon_name);

  return assoc;
}

static tracker_grl_sparql_t *
insert_key_mapping_with_setter (GrlKeyID                       grl_key,
                                const gchar                   *sparql_var_name,
                                const gchar                   *sparql_key_attr,
                                const gchar                   *sparql_key_attr_call,
                                const gchar                   *sparql_key_flavor,
                                GrlTypeFilter                  filter,
                                tracker_grl_sparql_setter_cb_t setter)
{
  tracker_grl_sparql_t *assoc;

  assoc = insert_key_mapping (grl_key,
                              sparql_var_name,
                              sparql_key_attr,
                              sparql_key_attr_call,
                              sparql_key_flavor,
                              filter);

  assoc->set_value = setter;

  return assoc;
}

void
grl_tracker_setup_key_mappings (void)
{
  GrlRegistry *registry = grl_registry_get_default ();
  GrlKeyID grl_metadata_key_chromaprint;

  grl_metadata_key_tracker_urn =
    grl_registry_lookup_metadata_key (registry, "tracker-urn");

  grl_metadata_key_gibest_hash =
    grl_registry_lookup_metadata_key (registry, "gibest-hash");

  grl_metadata_key_chromaprint =
    grl_registry_lookup_metadata_key (registry, "chromaprint");

  grl_to_sparql_mapping = g_hash_table_new (g_direct_hash, g_direct_equal);
  sparql_to_grl_mapping = g_hash_table_new (g_str_hash, g_str_equal);

  insert_key_mapping (grl_metadata_key_tracker_urn,
                      "urn",
                      NULL,
                      "?urn",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (GRL_METADATA_KEY_ALBUM,
                      "album",
                      NULL,
                      "nie:title(nmm:musicAlbum(?urn))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_ALBUM_DISC_NUMBER,
                      "albumDiscNumber",
                      NULL,
                      "nmm:setNumber(nmm:musicAlbumDisc(?urn))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_ARTIST,
                      "artist",
                      NULL,
                      "nmm:artistName(nmm:performer(?urn))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_ALBUM_ARTIST,
                      "albumArtist",
                      NULL,
                      "nmm:artistName(nmm:albumArtist(nmm:musicAlbum(?urn)))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_AUTHOR,
                      "author",
                      NULL,
                      "nmm:artistName(nmm:performer(?urn))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_BITRATE,
                      "bitrate",
                      "nfo:averageBitrate",
                      "nfo:averageBitrate(?urn)",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO | GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping (GRL_METADATA_KEY_CHILDCOUNT,
                      "childCount",
                      "nfo:entryCounter",
                      "nfo:entryCounter(?urn)",
                      "directory",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (GRL_METADATA_KEY_COMPOSER,
                      "composer",
                      NULL,
                      "nmm:artistName(nmm:composer(?urn))",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_SIZE,
                      "size",
                      NULL,
                      "nie:byteSize(?urn)",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (grl_metadata_key_gibest_hash,
                      "gibestHash",
                      NULL,
                      "(select nfo:hashValue(?h) { ?urn nie:isStoredAs/nfo:hasHash ?h . ?h nfo:hashAlgorithm \"gibest\" })",
                      "video",
                      GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_MODIFICATION_DATE,
                                  "lastModified",
                                  "nie:contentLastModified",
                                  "nie:contentLastModified(?urn)",
                                  "file",
                                  GRL_TYPE_FILTER_ALL,
                                  set_date);

  insert_key_mapping (GRL_METADATA_KEY_DURATION,
                      "duration",
                      "nfo:duration",
                      "nfo:duration(?urn)",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO | GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping (GRL_METADATA_KEY_MB_TRACK_ID,
                      "mbTrack",
                      NULL,
                      "(SELECT tracker:referenceIdentifier(?t) AS ?t_id { ?urn tracker:hasExternalReference ?t . ?t tracker:referenceSource \"https://musicbrainz.org/doc/Track\" })",
		      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_MB_ARTIST_ID,
                      "mbArtist",
                      NULL,
		      "(SELECT tracker:referenceIdentifier(?a) AS ?a_id { ?urn nmm:performer ?artist . ?artist tracker:hasExternalReference ?a . ?a tracker:referenceSource \"https://musicbrainz.org/doc/Artist\" })",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_MB_RECORDING_ID,
                      "mbRecording",
                      NULL,
		      "(SELECT tracker:referenceIdentifier(?r) AS ?r_id { ?urn tracker:hasExternalReference ?r . ?r tracker:referenceSource \"https://musicbrainz.org/doc/Recording\" })",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_MB_RELEASE_ID,
                      "mbRelease",
                      NULL,
		      "(SELECT tracker:referenceIdentifier(?re) AS ?re_id { ?urn nmm:musicAlbum ?album . ?album tracker:hasExternalReference ?re . ?re tracker:referenceSource \"https://musicbrainz.org/doc/Release\" })",
                      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping (GRL_METADATA_KEY_MB_RELEASE_GROUP_ID,
                      "mbReleaseGroup",
                      NULL,
		      "(SELECT tracker:referenceIdentifier(?rg) AS ?rg_id { ?urn nmm:musicAlbum ?album . ?album tracker:hasExternalReference ?rg . ?rg tracker:referenceSource \"https://musicbrainz.org/doc/Release_Group\" })",
		      "audio",
                      GRL_TYPE_FILTER_AUDIO);

  if (grl_metadata_key_chromaprint != 0) {
    insert_key_mapping_with_setter (grl_metadata_key_chromaprint,
                                    "chromaprint",
                                    NULL,
                                    "(select nfo:hashValue(?h) { ?urn nie:isStoredAs/nfo:hasHash ?h . ?h nfo:hashAlgorithm \"chromaprint\" })",
                                    "audio",
                                    GRL_TYPE_FILTER_AUDIO,
                                    set_string_metadata_keys);
  };

  insert_key_mapping (GRL_METADATA_KEY_FRAMERATE,
                      "frameRate",
                      "nfo:frameRate",
                      "nfo:frameRate(?urn)",
                      "video",
                      GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping (GRL_METADATA_KEY_HEIGHT,
                      "height",
                      "nfo:height",
                      "nfo:height(?urn)",
                      "video",
                      GRL_TYPE_FILTER_VIDEO | GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping (GRL_METADATA_KEY_ID,
                      "id",
                      "id",
                      "?urn",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (GRL_METADATA_KEY_MIME,
                      "mimeType",
                      "nie:mimeType",
                      "nie:mimeType(?urn)",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (GRL_METADATA_KEY_SITE,
                      "siteUrl",
                      "nie:isStoredAs",
                      "nie:isStoredAs(?urn)",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_TITLE,
                                  "title",
                                  "nie:title",
                                  "nie:title(?urn)",
                                  "audio",
                                  GRL_TYPE_FILTER_ALL,
                                  set_title);

  insert_key_mapping (GRL_METADATA_KEY_URL,
                      "url",
                      "nie:isStoredAs",
                      "nie:isStoredAs(?urn)",
                      "file",
                      GRL_TYPE_FILTER_ALL);

  insert_key_mapping (GRL_METADATA_KEY_WIDTH,
                      "width",
                      "nfo:width",
                      "nfo:width(?urn)",
                      "video",
                      GRL_TYPE_FILTER_VIDEO | GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping (GRL_METADATA_KEY_SEASON,
                      "season",
                      NULL,
                      "nmm:seasonNumber(nmm:isPartOfSeason(?urn))",
                      "video",
                      GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping (GRL_METADATA_KEY_EPISODE,
                      "episode",
                      "nmm:episodeNumber",
                      "nmm:episodeNumber(?urn)",
                      "video",
                      GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_CREATION_DATE,
                                  "creationDate",
                                  "nie:contentCreated",
                                  "nie:contentCreated(?urn)",
                                  "image",
                                  GRL_TYPE_FILTER_ALL,
                                  set_date);

  insert_key_mapping (GRL_METADATA_KEY_CAMERA_MODEL,
                      "cameraModel",
                      NULL,
                      "nfo:model(nfo:equipment(?urn))",
                      "image",
                      GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping (GRL_METADATA_KEY_FLASH_USED,
                      "flashUsed",
                      "nmm:flash",
                      "nmm:flash(?urn)",
                      "image",
                      GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping (GRL_METADATA_KEY_EXPOSURE_TIME,
                      "exposureTime",
                      "nmm:exposureTime",
                      "nmm:exposureTime(?urn)",
                      "image",
                      GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping (GRL_METADATA_KEY_ISO_SPEED,
                      "isoSpeed",
                      "nmm:isoSpeed",
                      "nmm:isoSpeed(?urn)",
                      "image",
                      GRL_TYPE_FILTER_IMAGE);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_ORIENTATION,
                                  "orientation",
                                  "nfo:orientation",
                                  "nfo:orientation(?urn)",
                                  "image",
                                  GRL_TYPE_FILTER_IMAGE,
                                  set_orientation);

  insert_key_mapping (GRL_METADATA_KEY_PLAY_COUNT,
                      "playCount",
                      "nie:usageCounter",
                      "nie:usageCounter(?urn)",
                      "media",
                      GRL_TYPE_FILTER_AUDIO | GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_LAST_PLAYED,
                                  "lastPlayed",
                                  "nie:contentAccessed",
                                  "nie:contentAccessed(?urn)",
                                  "media",
                                  GRL_TYPE_FILTER_ALL,
                                  set_date);

  insert_key_mapping (GRL_METADATA_KEY_LAST_POSITION,
                      "lastPlayPosition",
                      "nfo:lastPlayedPosition",
                      "nfo:lastPlayedPosition(?urn)",
                      "media",
                      GRL_TYPE_FILTER_AUDIO | GRL_TYPE_FILTER_VIDEO);

  insert_key_mapping (GRL_METADATA_KEY_START_TIME,
                      "startTime",
                      "nfo:audioOffset",
                      "nfo:audioOffset(?urn)",
                      "media",
                      GRL_TYPE_FILTER_AUDIO);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_TRACK_NUMBER,
                                  "trackNumber",
                                  "nmm:trackNumber",
                                  "nmm:trackNumber(?urn)",
                                  "audio",
                                  GRL_TYPE_FILTER_AUDIO,
                                  set_int_metadata_keys);

  insert_key_mapping_with_setter (GRL_METADATA_KEY_FAVOURITE,
                                  "favorite",
                                  "nao:hasTag",
                                  "nao:hasTag(?urn)",
                                  "audio",
                                  GRL_TYPE_FILTER_ALL,
                                  set_favourite);
}

tracker_grl_sparql_t *
grl_tracker_get_mapping_from_sparql (const gchar *key)
{
  return (tracker_grl_sparql_t *) g_hash_table_lookup (sparql_to_grl_mapping,
                                                       key);
}

static GList *
get_mapping_from_grl (const GrlKeyID key)
{
  return (GList *) g_hash_table_lookup (grl_to_sparql_mapping,
                                        GRLKEYID_TO_POINTER (key));
}

gboolean
grl_tracker_key_is_supported (const GrlKeyID key)
{
  return g_hash_table_lookup (grl_to_sparql_mapping,
                              GRLKEYID_TO_POINTER (key)) != NULL;
}

/**/

gchar *
grl_tracker_source_get_select_string (const GList *keys)
{
  const GList *key = keys;
  GString *gstr = g_string_new ("");
  GList *assoc_list;
  tracker_grl_sparql_t *assoc;

  assoc_list = get_mapping_from_grl (grl_metadata_key_tracker_urn);
  assoc = (tracker_grl_sparql_t *) assoc_list->data;
  g_string_append_printf (gstr, "%s AS ?%s ",
                          assoc->sparql_key_attr_call,
                          assoc->sparql_key_name);

  while (key != NULL) {
    assoc_list = get_mapping_from_grl (GRLPOINTER_TO_KEYID (key->data));
    while (assoc_list != NULL) {
      assoc = (tracker_grl_sparql_t *) assoc_list->data;
      if (assoc != NULL) {
        g_string_append_printf (gstr, "%s AS ?%s ",
                                assoc->sparql_key_attr_call,
                                assoc->sparql_key_name);
      }
      assoc_list = assoc_list->next;
    }
    key = key->next;
  }

  return g_string_free (gstr, FALSE);
}

static void
gen_prop_insert_string (GString *gstr,
                        tracker_grl_sparql_t *assoc,
                        GrlData *data)
{
  gchar *tmp;
  GType type = GRL_METADATA_KEY_GET_TYPE (assoc->grl_key);

  switch (type) {
  case G_TYPE_STRING:
    tmp = g_strescape (grl_data_get_string (data, assoc->grl_key), NULL);
    g_string_append_printf (gstr, "%s \"%s\"",
                            assoc->sparql_key_attr, tmp);
    g_free (tmp);
    break;

  case G_TYPE_INT:
    g_string_append_printf (gstr, "%s %i",
                            assoc->sparql_key_attr,
                            grl_data_get_int (data, assoc->grl_key));
    break;

  case G_TYPE_FLOAT:
    g_string_append_printf (gstr, "%s %f",
                            assoc->sparql_key_attr,
                            grl_data_get_float (data, assoc->grl_key));
    break;

  case G_TYPE_BOOLEAN:
    /* Special case for favourite tag, see comment in
     * grl_tracker_tracker_get_insert_string for more details.
     */
    if (assoc->grl_key == GRL_METADATA_KEY_FAVOURITE) {
      g_string_append_printf (gstr, "%s nao:predefined-tag-favorite",
                              assoc->sparql_key_attr);
    }
    break;

  default:
    if (type == G_TYPE_DATE_TIME) {
      tmp = g_date_time_format (grl_data_get_boxed (data, assoc->grl_key),
                                "%FT%T%:z");
      g_string_append_printf (gstr, "%s '%s'",
                              assoc->sparql_key_attr,
                              tmp);

      g_free (tmp);
    }
    break;
  }
}

gchar *
grl_tracker_tracker_get_insert_string (GrlMedia *media, const GList *keys)
{
  gboolean first = TRUE;
  const GList *key;
  GString *gstr = g_string_new ("");

  for (key = keys; key != NULL; key = key->next) {
    const GList *assoc_list;
    GrlKeyID key_id = GRLPOINTER_TO_KEYID (key->data);

    for (assoc_list = get_mapping_from_grl (key_id);
         assoc_list != NULL;
         assoc_list = assoc_list->next) {
      tracker_grl_sparql_t *assoc = assoc_list->data;

      if (assoc == NULL)
        continue;

      /* The favourite key is really setting or deleting a tag
       * in tracker, so in the case of setting it to false skip
       * the insert string creation step for this key completely.
       */
      if (assoc->grl_key == GRL_METADATA_KEY_FAVOURITE &&
          !grl_media_get_favourite (media))
        continue;

      if (!grl_data_has_key (GRL_DATA (media), key_id))
        continue;

      /* Special case for key title, nfo:fileName is read-only.
       * It cannot be modified.
       */
      if (assoc->grl_key == GRL_METADATA_KEY_TITLE &&
          g_strcmp0 (assoc->sparql_key_attr, "nfo:fileName") == 0) {
        continue;
      }

      if (!first)
        g_string_append (gstr, " ; ");

      gen_prop_insert_string (gstr, assoc, GRL_DATA (media));
      first = FALSE;
    }
  }

  return g_string_free (gstr, FALSE);
}

gchar *
grl_tracker_get_delete_string (const GList *keys)
{
  gboolean first = TRUE;
  const GList *key = keys, *assoc_list;
  tracker_grl_sparql_t *assoc;
  GString *gstr = g_string_new ("");
  gchar *ret;
  gint var_n = 0;

  while (key != NULL) {
    assoc_list = get_mapping_from_grl (GRLPOINTER_TO_KEYID (key->data));
    while (assoc_list != NULL) {
      assoc = (tracker_grl_sparql_t *) assoc_list->data;
      if (assoc != NULL) {
        /* Special case for key title, nfo:fileName is read-only.
         * It cannot be modified.
         */
        if (assoc->grl_key == GRL_METADATA_KEY_TITLE &&
            g_strcmp0 (assoc->sparql_key_attr, "nfo:fileName") == 0) {
          assoc_list = assoc_list->next;
          continue;
        }

        if (first) {
          g_string_append_printf (gstr, "%s ?v%i",
                                  assoc->sparql_key_attr, var_n);
          first = FALSE;
        } else {
          g_string_append_printf (gstr, " ; %s ?v%i",
                                  assoc->sparql_key_attr, var_n);
        }
        var_n++;
      }
      assoc_list = assoc_list->next;
    }
    key = key->next;
  }

  ret = gstr->str;
  g_string_free (gstr, FALSE);

  return ret;
}

gchar *
grl_tracker_get_delete_conditional_string (const gchar *urn,
                                           const GList *keys)
{
  gboolean first = TRUE;
  const GList *key = keys, *assoc_list;
  tracker_grl_sparql_t *assoc;
  GString *gstr = g_string_new ("");
  gchar *ret;
  gint var_n = 0;

  while (key != NULL) {
    assoc_list = get_mapping_from_grl (GRLPOINTER_TO_KEYID (key->data));
    while (assoc_list != NULL) {
      assoc = (tracker_grl_sparql_t *) assoc_list->data;
      if (assoc != NULL) {
        /* Special case for key title, nfo:fileName is read-only.
         * It cannot be modified.
         */
        if (assoc->grl_key == GRL_METADATA_KEY_TITLE &&
            g_strcmp0 (assoc->sparql_key_attr, "nfo:fileName") == 0) {
          assoc_list = assoc_list->next;
          continue;
        }

        if (first) {
          g_string_append_printf (gstr, "OPTIONAL { <%s>  %s ?v%i }",
                                  urn,  assoc->sparql_key_attr, var_n);
          first = FALSE;
        } else {
          g_string_append_printf (gstr, " . OPTIONAL { <%s> %s ?v%i }",
                                  urn, assoc->sparql_key_attr, var_n);
        }
        var_n++;
      }
      assoc_list = assoc_list->next;
    }
    key = key->next;
  }

  ret = gstr->str;
  g_string_free (gstr, FALSE);

  return ret;
}

static GrlMedia *
grl_tracker_build_grilo_media_default (GHashTable *ht)
{
  if (g_hash_table_lookup (ht, RDF_TYPE_MUSIC)) {
    return grl_media_audio_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_VIDEO)) {
    return grl_media_video_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_IMAGE)) {
    return grl_media_image_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_ARTIST)) {
    return grl_media_container_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_ALBUM)) {
    return grl_media_container_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_CONTAINER)) {
    return grl_media_container_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_FOLDER)) {
    return grl_media_container_new ();
  } else if (g_hash_table_lookup (ht, RDF_TYPE_PLAYLIST)) {
    return grl_media_container_new ();
  }

  return NULL;
}

/**/

/* Builds an appropriate GrlMedia based on ontology type returned by
   tracker, or NULL if unknown */
GrlMedia *
grl_tracker_build_grilo_media (const gchar   *rdf_type,
                               GrlTypeFilter  type_filter)
{
  GrlMedia *media = NULL;
  gchar **rdf_single_type;
  int i;
  GHashTable *ht;

  if (!rdf_type) {
    return NULL;
  }

  /* As rdf_type can be formed by several types, split them */
  rdf_single_type = g_strsplit (rdf_type, ",", -1);
  i = g_strv_length (rdf_single_type) - 1;
  ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  for (; i>= 0; i--)
    g_hash_table_insert (ht, g_path_get_basename (rdf_single_type[i]), GINT_TO_POINTER(TRUE));

  if (type_filter == GRL_TYPE_FILTER_NONE ||
      type_filter == GRL_TYPE_FILTER_ALL) {
    media = grl_tracker_build_grilo_media_default (ht);
  } else if ((type_filter & GRL_TYPE_FILTER_AUDIO) &&
             g_hash_table_lookup (ht, RDF_TYPE_MUSIC)) {
    media = grl_media_audio_new ();
  } else if ((type_filter & GRL_TYPE_FILTER_VIDEO) &&
             g_hash_table_lookup (ht, RDF_TYPE_VIDEO)) {
    media = grl_media_video_new ();
  } else if ((type_filter & GRL_TYPE_FILTER_IMAGE) &&
             g_hash_table_lookup (ht, RDF_TYPE_IMAGE)) {
    media = grl_media_image_new ();
  } else {
    media = grl_tracker_build_grilo_media_default (ht);
  }

  g_hash_table_destroy (ht);
  g_strfreev (rdf_single_type);

  if (!media)
    media = grl_media_new ();

  return media;
}

/**/

const GList *
grl_tracker_supported_keys (GrlSource *source)
{
  static GList *supported_keys = NULL;

  if (!supported_keys) {
    supported_keys =  g_hash_table_get_keys (grl_to_sparql_mapping);
  }

  return supported_keys;
}

const gchar *
grl_tracker_key_get_variable_name (const GrlKeyID key)
{
  tracker_grl_sparql_t *assoc;
  GList *assoc_list;

  assoc_list = g_hash_table_lookup (grl_to_sparql_mapping,
                                    GRLKEYID_TO_POINTER (key));
  if (!assoc_list)
    return NULL;
  assoc = assoc_list->data;

  return assoc->sparql_var_name;
}

const gchar *
grl_tracker_key_get_sparql_statement (const GrlKeyID key,
                                      GrlTypeFilter  filter)
{
  tracker_grl_sparql_t *assoc;
  GList *assoc_list;

  assoc_list = g_hash_table_lookup (grl_to_sparql_mapping,
                                    GRLKEYID_TO_POINTER (key));
  if (!assoc_list)
    return NULL;

  assoc = assoc_list->data;
  if ((assoc->filter & filter) == 0)
    return NULL;

  return assoc->sparql_key_attr_call;
}
