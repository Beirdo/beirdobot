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

char *
mrss_strerror (mrss_error_t err)
{
  switch (err)
    {
    case MRSS_OK:
      return "Success";

    case MRSS_ERR_PARSER:
      return "Parser error";

    case MRSS_ERR_VERSION:
      return "Version error";

    case MRSS_ERR_DATA:
      return "No correct paramenter in the function";

    default:
      return strerror (errno);
    }
}

mrss_error_t
mrss_element (mrss_generic_t element, mrss_element_t * ret)
{
  mrss_t *tmp;

  if (!element || !ret)
    return MRSS_ERR_DATA;

  tmp = (mrss_t *) element;
  *ret = tmp->element;
  return MRSS_OK;
}

static size_t
__mrss_get_last_modified_header (void *ptr, size_t size, size_t nmemb,
				 time_t * timing)
{
  char *header = (char *) ptr;

  if (!strncmp ("Last-Modified:", header, 14))
    *timing = curl_getdate (header + 14, NULL);

  return size * nmemb;
}

mrss_error_t
mrss_get_last_modified (char *urlstring, time_t * lastmodified)
{
  CURL *curl;
  int timeout;

  if (!urlstring || !lastmodified)
    return MRSS_ERR_DATA;

  *lastmodified = 0;

  curl_global_init (CURL_GLOBAL_DEFAULT);
  if (!(curl = curl_easy_init ()))
    return MRSS_ERR_POSIX;

  curl_easy_setopt (curl, CURLOPT_URL, urlstring);
  curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION,
		    __mrss_get_last_modified_header);
  curl_easy_setopt (curl, CURLOPT_HEADERDATA, lastmodified);
  curl_easy_setopt (curl, CURLOPT_NOBODY, 1);
  curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);

  if (mrss_get_timeout (&timeout) == MRSS_OK)
    {
      if (timeout > 0)
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeout);
      else if (timeout < 0)
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10);
    }

  if (curl_easy_perform (curl))
    {
      curl_easy_cleanup (curl);
      return MRSS_ERR_POSIX;
    }

  curl_easy_cleanup (curl);

  return MRSS_OK;
}

/* EOF */
