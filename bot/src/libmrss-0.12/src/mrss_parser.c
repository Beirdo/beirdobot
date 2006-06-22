/* mRss - Copyright (C) 2005-2006 bakunin - Andrea Marchesini 
 *                                    <bakunin@autistici.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#else
# error Use configure; make; make install
#endif

#include "mrss.h"
#include "mrss_internal.h"

static int __mrss_timeout = 0;

static void
__mrss_parser_rss_image (nxml_t * doc, nxml_data_t * cur, mrss_t * data)
{
  char *c;

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  /* title */
	  if (!strcmp (cur->value, "title") && !data->image_title
	      && (c = nxmle_get_string (cur, NULL)))
	    data->image_title = c;

	  /* url */
	  else if (!strcmp (cur->value, "url") && !data->image_url
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      free (c);
	    }

	  /* link */
	  else if (!strcmp (cur->value, "link") && !data->image_link
		   && (c = nxmle_get_string (cur, NULL)))
	    data->image_link = c;

	  /* width */
	  else if (!strcmp (cur->value, "width") && !data->image_width
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      data->image_width = atoi (c);
	      free (c);
	    }

	  /* height */
	  else if (!strcmp (cur->value, "height") && !data->image_height
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      data->image_height = atoi (c);
	      free (c);
	    }

	  /* description */
	  else if (!strcmp (cur->value, "description")
		   && !data->image_description
		   && (c = nxmle_get_string (cur, NULL)))
	    data->image_description = c;
	}
    }
}

static void
__mrss_parser_rss_textinput (nxml_t * doc, nxml_data_t * cur, mrss_t * data)
{
  char *c;

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  /* title */
	  if (!strcmp (cur->value, "title") && !data->textinput_title
	      && (c = nxmle_get_string (cur, NULL)))
	    data->textinput_title = c;

	  /* description */
	  else if (!strcmp (cur->value, "description")
		   && !data->textinput_description
		   && (c = nxmle_get_string (cur, NULL)))
	    data->textinput_description = c;

	  /* name */
	  else if (!strcmp (cur->value, "name") && !data->textinput_name
		   && (c = nxmle_get_string (cur, NULL)))
	    data->textinput_name = c;

	  /* link */
	  else if (!strcmp (cur->value, "link") && !data->textinput_link
		   && (c = nxmle_get_string (cur, NULL)))
	    data->textinput_link = c;
	}
    }
}

static void
__mrss_parser_rss_skipHours (nxml_t * doc, nxml_data_t * cur, mrss_t * data)
{
  char *c;

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  if (!strcmp (cur->value, "hour")
	      && (c = nxmle_get_string (cur, NULL)))
	    {
	      mrss_hour_t *hour;

	      if (!(hour = (mrss_hour_t *) malloc (sizeof (mrss_hour_t))))
		{
		  free (c);
		  return;
		}

	      memset (hour, 0, sizeof (mrss_hour_t));
	      hour->element = MRSS_ELEMENT_SKIPHOURS;
	      hour->allocated = 1;
	      hour->hour = c;

	      if (!data->skipHours)
		data->skipHours = hour;
	      else
		{
		  mrss_hour_t *tmp;

		  tmp = data->skipHours;

		  while (tmp->next)
		    tmp = tmp->next;
		  tmp->next = hour;
		}
	    }
	}
    }
}

static void
__mrss_parser_rss_skipDays (nxml_t * doc, nxml_data_t * cur, mrss_t * data)
{
  char *c;

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  if (!strcmp (cur->value, "day")
	      && (c = nxmle_get_string (cur, NULL)))
	    {
	      mrss_day_t *day;

	      if (!(day = (mrss_day_t *) malloc (sizeof (mrss_day_t))))
		{
		  free (c);
		  return;
		}

	      memset (day, 0, sizeof (mrss_day_t));
	      day->element = MRSS_ELEMENT_SKIPDAYS;
	      day->allocated = 1;
	      day->day = c;

	      if (!data->skipDays)
		data->skipDays = day;
	      else
		{
		  mrss_day_t *tmp;

		  tmp = data->skipDays;

		  while (tmp->next)
		    tmp = tmp->next;
		  tmp->next = day;
		}
	    }
	}
    }
}

static void
__mrss_parser_rss_item (nxml_t * doc, nxml_data_t * cur, mrss_t * data)
{
  char *c;
  char *attr;
  mrss_item_t *item;

  if (!(item = (mrss_item_t *) malloc (sizeof (mrss_item_t))))
    return;

  memset (item, 0, sizeof (mrss_item_t));
  item->element = MRSS_ELEMENT_ITEM;
  item->allocated = 1;

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  /* title */
	  if (!strcmp (cur->value, "title") && !item->title
	      && (c = nxmle_get_string (cur, NULL)))
	    item->title = c;

	  /* link */
	  else if (!strcmp (cur->value, "link") && !item->link
		   && (c = nxmle_get_string (cur, NULL)))
	    item->link = c;

	  /* description */
	  else if (!strcmp (cur->value, "description") && !item->description
		   && (c = nxmle_get_string (cur, NULL)))
	    item->description = c;

	  /* source */
	  else if (!strcmp (cur->value, "source") && !item->source)
	    {
	      item->source = nxmle_get_string (cur, NULL);

	      if ((attr = nxmle_find_attribute (cur, "url", NULL)))
		item->source_url = attr;
	    }

	  /* enclosure */
	  else if (!strcmp (cur->value, "enclosure") && !item->enclosure)
	    {
	      item->enclosure = nxmle_get_string (cur, NULL);

	      if ((attr = nxmle_find_attribute (cur, "url", NULL)))
		item->enclosure_url = attr;

	      if ((attr = nxmle_find_attribute (cur, "length", NULL)))
		{
		  item->enclosure_length = atoi (attr);
		  free (attr);
		}

	      if ((attr = nxmle_find_attribute (cur, "type", NULL)))
		item->enclosure_type = attr;
	    }

	  /* category */
	  else if (!strcmp (cur->value, "category")
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      mrss_category_t *category;

	      if (!
		  (category =
		   (mrss_category_t *) malloc (sizeof (mrss_category_t))))
		{
		  free (c);
		  return;
		}

	      memset (category, 0, sizeof (mrss_category_t));

	      category->element = MRSS_ELEMENT_CATEGORY;
	      category->allocated = 1;
	      category->category = c;

	      if ((attr = nxmle_find_attribute (cur, "domain", NULL)))
		category->domain = attr;

	      if (!item->category)
		item->category = category;
	      else
		{
		  mrss_category_t *tmp;

		  tmp = item->category;
		  while (tmp->next)
		    tmp = tmp->next;
		  tmp->next = category;
		}
	    }

	  /* author */
	  else if (!strcmp (cur->value, "author") && !item->author
		   && (c = nxmle_get_string (cur, NULL)))
	    item->author = c;

	  /* comments */
	  else if (!strcmp (cur->value, "comments") && !item->comments
		   && (c = nxmle_get_string (cur, NULL)))
	    item->comments = c;

	  /* guid */
	  else if (!strcmp (cur->value, "guid") && !item->guid
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      item->guid = c;

	      if ((attr = nxmle_find_attribute (cur, "isPermaLink", NULL)))
		{
		  if (!strcmp (attr, "false"))
		    item->guid_isPermaLink = 0;
		  else
		    item->guid_isPermaLink = 1;

		  free (attr);
		}
	    }

	  /* pubDate */
	  else if (!strcmp (cur->value, "pubDate") && !item->pubDate
		   && (c = nxmle_get_string (cur, NULL)))
	    item->pubDate = c;

	}
    }


  if (!data->item)
    data->item = item;
  else
    {
      mrss_item_t *tmp;

      tmp = data->item;

      while (tmp->next)
	tmp = tmp->next;
      tmp->next = item;
    }
}

static mrss_error_t
__mrss_parser_rss (mrss_version_t v, nxml_t * doc, nxml_data_t * cur,
		   mrss_t ** ret)
{
  mrss_t *data;
  char *c, *attr;

  if (!(data = (mrss_t *) malloc (sizeof (mrss_t))))
    return MRSS_ERR_POSIX;

  memset (data, 0, sizeof (mrss_t));
  data->element = MRSS_ELEMENT_CHANNEL;
  data->allocated = 1;
  data->version = v;

  if (doc->encoding && !(data->encoding = strdup (doc->encoding)))
    {
      mrss_free (data);
      return MRSS_ERR_POSIX;
    }

  if (data->version == MRSS_VERSION_1_0)
    {
      nxml_data_t *cur_channel = NULL;

      while (cur)
	{

	  if (!strcmp (cur->value, "channel"))
	    cur_channel = cur;

	  else if (!strcmp (cur->value, "image"))
	    __mrss_parser_rss_image (doc, cur, data);

	  else if (!strcmp (cur->value, "textinput"))
	    __mrss_parser_rss_textinput (doc, cur, data);

	  else if (!strcmp (cur->value, "item"))
	    __mrss_parser_rss_item (doc, cur, data);

	  cur = cur->next;
	}

      cur = cur_channel;
    }
  else
    {
      while (cur && strcmp (cur->value, "channel"))
	cur = cur->next;
    }

  if (!cur)
    {
      mrss_free (data);
      return MRSS_ERR_PARSER;
    }

  if (data->version == MRSS_VERSION_1_0)
    {
      if ((attr = nxmle_find_attribute (cur, "about", NULL)))
	data->about = attr;
    }

  for (cur = cur->children; cur; cur = cur->next)
    {
      if (cur->type == NXML_TYPE_ELEMENT)
	{
	  /* title */
	  if (!strcmp (cur->value, "title") && !data->title &&
	      (c = nxmle_get_string (cur, NULL)))
	    data->title = c;

	  /* description */
	  else if (!strcmp (cur->value, "description") && !data->description
		   && (c = nxmle_get_string (cur, NULL)))
	    data->description = c;

	  /* link */
	  else if (!strcmp (cur->value, "link") && !data->link
		   && (c = nxmle_get_string (cur, NULL)))
	    data->link = c;

	  /* language */
	  else if (!strcmp (cur->value, "language") && !data->language
		   && (c = nxmle_get_string (cur, NULL)))
	    data->language = c;

	  /* rating */
	  else if (!strcmp (cur->value, "rating") && !data->rating
		   && (c = nxmle_get_string (cur, NULL)))
	    data->rating = c;

	  /* copyright */
	  else if (!strcmp (cur->value, "copyright") && !data->copyright
		   && (c = nxmle_get_string (cur, NULL)))
	    data->copyright = c;

	  /* pubDate */
	  else if (!strcmp (cur->value, "pubDate") && !data->pubDate
		   && (c = nxmle_get_string (cur, NULL)))
	    data->pubDate = c;

	  /* lastBuildDate */
	  else if (!strcmp (cur->value, "lastBuildDate")
		   && !data->lastBuildDate
		   && (c = nxmle_get_string (cur, NULL)))
	    data->lastBuildDate = c;

	  /* docs */
	  else if (!strcmp (cur->value, "docs") && !data->docs
		   && (c = nxmle_get_string (cur, NULL)))
	    data->docs = c;

	  /* managingeditor */
	  else if (!strcmp (cur->value, "managingeditor")
		   && !data->managingeditor
		   && (c = nxmle_get_string (cur, NULL)))
	    data->managingeditor = c;

	  /* webMaster */
	  else if (!strcmp (cur->value, "webMaster") && !data->webMaster
		   && (c = nxmle_get_string (cur, NULL)))
	    data->webMaster = c;

	  /* image */
	  else if (!strcmp (cur->value, "image"))
	    __mrss_parser_rss_image (doc, cur, data);

	  /* textinput */
	  else if (!strcmp (cur->value, "textinput"))
	    __mrss_parser_rss_textinput (doc, cur, data);

	  /* skipHours */
	  else if (!strcmp (cur->value, "skipHours"))
	    __mrss_parser_rss_skipHours (doc, cur, data);

	  /* skipDays */
	  else if (!strcmp (cur->value, "skipDays"))
	    __mrss_parser_rss_skipDays (doc, cur, data);

	  /* item */
	  else if (!strcmp (cur->value, "item"))
	    __mrss_parser_rss_item (doc, cur, data);

	  /* category */
	  else if (!strcmp (cur->value, "category")
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      mrss_category_t *category;

	      if (!
		  (category =
		   (mrss_category_t *) malloc (sizeof (mrss_category_t))))
		{
		  mrss_free ((mrss_generic_t *) data);
		  free (c);
		  return MRSS_ERR_POSIX;
		}

	      memset (category, 0, sizeof (mrss_category_t));

	      category->element = MRSS_ELEMENT_CATEGORY;
	      category->allocated = 1;
	      category->category = c;

	      if ((attr = nxmle_find_attribute (cur, "domain", NULL)))
		category->domain = attr;

	      if (!data->category)
		data->category = category;
	      else
		{
		  mrss_category_t *tmp;

		  tmp = data->category;
		  while (tmp->next)
		    tmp = tmp->next;
		  tmp->next = category;
		}
	    }

	  /* enclosure */
	  else if (!strcmp (cur->value, "cloud") && !data->cloud)
	    {
	      data->cloud = nxmle_get_string (cur, NULL);

	      if (!data->cloud_domain
		  && (attr = nxmle_find_attribute (cur, "domain", NULL)))
		data->cloud_domain = attr;

	      if (!data->cloud_port
		  && (attr = nxmle_find_attribute (cur, "port", NULL)))
		data->cloud_port = atoi (attr);

	      if (!data->cloud_registerProcedure
		  && (attr =
		      nxmle_find_attribute (cur, "registerProcedure", NULL)))
		data->cloud_registerProcedure = attr;

	      if (!data->cloud_protocol
		  && (attr = nxmle_find_attribute (cur, "protocol", NULL)))
		data->cloud_protocol = attr;
	    }

	  /* generator */
	  else if (!strcmp (cur->value, "generator") && !data->generator
		   && (c = nxmle_get_string (cur, NULL)))
	    data->generator = c;

	  /* ttl */
	  else if (!strcmp (cur->value, "ttl") && !data->ttl
		   && (c = nxmle_get_string (cur, NULL)))
	    {
	      data->ttl = atoi (c);
	      free (c);
	    }

	}
    }

  *ret = data;

  return MRSS_OK;
}

static mrss_error_t
__mrss_parser (nxml_t * doc, mrss_t ** ret)
{
  mrss_error_t r = MRSS_ERR_VERSION;
  nxml_data_t *cur;
  char *c;

  if (!(cur = nxmle_root_element (doc, NULL)))
    return MRSS_ERR_PARSER;

  if (!strcmp (cur->value, "rss"))
    {
      if ((c = nxmle_find_attribute (cur, "version", NULL)))
	{
	  /* 0.91 VERSION */
	  if (!strcmp (c, "0.91"))
	    r =
	      __mrss_parser_rss (MRSS_VERSION_0_91, doc, cur->children, ret);

	  /* 0.92 VERSION */
	  else if (!strcmp (c, "0.92"))
	    r =
	      __mrss_parser_rss (MRSS_VERSION_0_92, doc, cur->children, ret);

	  /* 2.0 VERSION */
	  else if (!strcmp (c, "2.0"))
	    r = __mrss_parser_rss (MRSS_VERSION_2_0, doc, cur->children, ret);

	  else
	    r = MRSS_ERR_VERSION;

	  free (c);
	}

      else
	r = MRSS_ERR_VERSION;
    }

  else if (!strcmp (cur->value, "RDF"))
    r = __mrss_parser_rss (MRSS_VERSION_1_0, doc, cur->children, ret);

  else
    r = MRSS_ERR_PARSER;

  return r;
}

/*************************** EXTERNAL FUNCTION ******************************/

mrss_error_t
mrss_parse_url (char *url, mrss_t ** ret)
{
  __mrss_download_t *download;
  nxml_t *doc;
  mrss_error_t err;

  if (!url || !ret)
    return MRSS_ERR_DATA;

  if (!(download = __mrss_download_file (url, __mrss_timeout)))
    return MRSS_ERR_POSIX;

  if (nxml_new (&doc) != NXML_OK)
    return MRSS_ERR_POSIX;

  if (nxml_parse_buffer (doc, download->mm, download->size) != NXML_OK)
    {
      free (download->mm);
      free (download);

      nxml_free (doc);

      return MRSS_ERR_PARSER;
    }

  if (!(err = __mrss_parser (doc, ret)))
    {
      if (!((*ret)->file = strdup (url)))
	{
	  free (download->mm);
	  free (download);

	  mrss_free (*ret);
	  nxml_free (doc);

	  return MRSS_ERR_POSIX;
	}

      (*ret)->size = download->size;
    }

  free (download->mm);
  free (download);

  nxml_free (doc);

  return err;
}

mrss_error_t
mrss_parse_file (char *file, mrss_t ** ret)
{
  nxml_t *doc;
  mrss_error_t err;
  struct stat st;

  if (!file || !ret)
    return MRSS_ERR_DATA;

  if (lstat (file, &st))
    return MRSS_ERR_POSIX;

  if (nxml_new (&doc) != NXML_OK)
    return MRSS_ERR_POSIX;

  if (nxml_parse_file (doc, file) != NXML_OK)
    {
      nxml_free (doc);
      return MRSS_ERR_PARSER;
    }

  if (!(err = __mrss_parser (doc, ret)))
    {
      if (!((*ret)->file = strdup (file)))
	{
	  nxml_free (doc);
	  mrss_free (*ret);

	  return MRSS_ERR_POSIX;
	}

      (*ret)->size = st.st_size;
    }

  nxml_free (doc);

  return err;
}

mrss_error_t
mrss_parse_buffer (char *buffer, size_t size, mrss_t ** ret)
{
  nxml_t *doc;
  mrss_error_t err;

  if (!buffer || !size || !ret)
    return MRSS_ERR_DATA;

  if (nxml_new (&doc) != NXML_OK)
    return MRSS_ERR_POSIX;

  if (nxml_parse_buffer (doc, buffer, size))
    {
      nxml_free (doc);
      return MRSS_ERR_PARSER;
    }

  if (!(err = __mrss_parser (doc, ret)))
    (*ret)->size = size;

  nxml_free (doc);
  return err;
}

mrss_error_t
mrss_set_timeout (int timeout)
{
  __mrss_timeout = timeout;
  return MRSS_OK;
}

mrss_error_t
mrss_get_timeout (int *timeout)
{
  if (!timeout)
    return MRSS_ERR_DATA;

  *timeout = __mrss_timeout;
  return MRSS_OK;
}

/* EOF */
