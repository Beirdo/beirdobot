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

static void __mrss_free_channel (mrss_t * mrss);
static void __mrss_free_category (mrss_category_t * category);
static void __mrss_free_hour (mrss_hour_t * hour);
static void __mrss_free_day (mrss_day_t * day);
static void __mrss_free_item (mrss_item_t * item);

static void
__mrss_free_channel (mrss_t * mrss)
{
  mrss_hour_t *hour;
  mrss_day_t *day;
  mrss_category_t *category;
  mrss_item_t *item;
  void *old;

  if (!mrss)
    return;

  if (mrss->file)
    free (mrss->file);

  if (mrss->encoding)
    free (mrss->encoding);

  if (mrss->title)
    free (mrss->title);

  if (mrss->description)
    free (mrss->description);

  if (mrss->link)
    free (mrss->link);

  if (mrss->language)
    free (mrss->language);

  if (mrss->rating)
    free (mrss->rating);

  if (mrss->copyright)
    free (mrss->copyright);

  if (mrss->pubDate)
    free (mrss->pubDate);

  if (mrss->lastBuildDate)
    free (mrss->lastBuildDate);

  if (mrss->docs)
    free (mrss->docs);

  if (mrss->managingeditor)
    free (mrss->managingeditor);

  if (mrss->webMaster)
    free (mrss->webMaster);

  if (mrss->about)
    free (mrss->about);

  if (mrss->image_title)
    free (mrss->image_title);

  if (mrss->image_url)
    free (mrss->image_url);

  if (mrss->image_link)
    free (mrss->image_link);

  if (mrss->image_description)
    free (mrss->image_description);

  if (mrss->textinput_title)
    free (mrss->textinput_title);

  if (mrss->textinput_description)
    free (mrss->textinput_description);

  if (mrss->textinput_name)
    free (mrss->textinput_name);

  if (mrss->textinput_link)
    free (mrss->textinput_link);

  if (mrss->generator)
    free (mrss->generator);

  if (mrss->cloud)
    free (mrss->cloud);

  if (mrss->cloud_domain)
    free (mrss->cloud_domain);

  if (mrss->cloud_path)
    free (mrss->cloud_path);

  if (mrss->cloud_registerProcedure)
    free (mrss->cloud_registerProcedure);

  if (mrss->cloud_protocol)
    free (mrss->cloud_protocol);

  category = mrss->category;
  while (category)
    {
      old = category;
      category = category->next;

      __mrss_free_category ((mrss_category_t *) old);
    }

  hour = mrss->skipHours;
  while (hour)
    {
      old = hour;
      hour = hour->next;

      __mrss_free_hour ((mrss_hour_t *) old);
    }

  day = mrss->skipDays;
  while (day)
    {
      old = day;
      day = day->next;

      __mrss_free_day ((mrss_day_t *) old);
    }

  item = mrss->item;
  while (item)
    {
      old = item;
      item = item->next;

      __mrss_free_item ((mrss_item_t *) old);
    }

  if (mrss->allocated)
    free (mrss);
}

static void
__mrss_free_category (mrss_category_t * category)
{
  if (!category)
    return;

  if (category->category)
    free (category->category);

  if (category->domain)
    free (category->domain);

  if (category->allocated)
    free (category);
}

static void
__mrss_free_hour (mrss_hour_t * hour)
{
  if (!hour)
    return;

  if (hour->hour)
    free (hour->hour);

  if (hour->allocated)
    free (hour);
}

static void
__mrss_free_day (mrss_day_t * day)
{
  if (!day)
    return;

  if (day->day)
    free (day->day);

  if (day->allocated)
    free (day);
}

static void
__mrss_free_item (mrss_item_t * item)
{
  mrss_category_t *category, *old;

  if (!item)
    return;

  if (item->title)
    free (item->title);

  if (item->link)
    free (item->link);

  if (item->description)
    free (item->description);

  if (item->author)
    free (item->author);

  if (item->comments)
    free (item->comments);

  if (item->pubDate)
    free (item->pubDate);

  if (item->guid)
    free (item->guid);

  if (item->source)
    free (item->source);

  if (item->source_url)
    free (item->source_url);

  if (item->enclosure)
    free (item->enclosure);

  if (item->enclosure_url)
    free (item->enclosure_url);

  if (item->enclosure_type)
    free (item->enclosure_type);

  category = item->category;
  while (category)
    {
      old = category;
      category = category->next;

      __mrss_free_category (old);
    }

  if (item->allocated)
    free (item);
}

/*************************** EXTERNAL FUNCTION ******************************/

mrss_error_t
mrss_free (mrss_generic_t element)
{
  mrss_t *tmp;

  tmp = (mrss_t *) element;

  switch (tmp->element)
    {
    case MRSS_ELEMENT_CHANNEL:
      __mrss_free_channel ((mrss_t *) element);
      break;

    case MRSS_ELEMENT_ITEM:
      __mrss_free_item ((mrss_item_t *) element);
      break;

    case MRSS_ELEMENT_SKIPHOURS:
      __mrss_free_hour ((mrss_hour_t *) element);
      break;

    case MRSS_ELEMENT_SKIPDAYS:
      __mrss_free_day ((mrss_day_t *) element);
      break;

    case MRSS_ELEMENT_CATEGORY:
      __mrss_free_category ((mrss_category_t *) element);
      break;

    default:
      return MRSS_ERR_DATA;
    }

  return MRSS_OK;
}

/* EOF */
