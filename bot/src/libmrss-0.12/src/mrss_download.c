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

static size_t
__mrss_memorize_file (void *ptr, size_t size, size_t nmemb, void *data)
{
  register int realsize = size * nmemb;
  __mrss_download_t *mem = (__mrss_download_t *) data;

  if (!mem->mm)
    {
      if (!(mem->mm = (char *) malloc (realsize + 1)))
	return -1;
    }
  else
    {
      if (!(mem->mm = (char *) realloc (mem->mm, mem->size + realsize + 1)))
	return -1;
    }

  memcpy (&(mem->mm[mem->size]), ptr, realsize);
  mem->size += realsize;
  mem->mm[mem->size] = 0;

  return realsize;
}

__mrss_download_t *
__mrss_download_file (char *fl, int timeout)
{
  __mrss_download_t *chunk;
  CURL *curl;

  if (!(chunk = (__mrss_download_t *) malloc (sizeof (__mrss_download_t))))
    return NULL;

  chunk->mm = NULL;
  chunk->size = 0;

  curl_global_init (CURL_GLOBAL_DEFAULT);
  if (!(curl = curl_easy_init ()))
    {
      if (chunk->mm)
	free (chunk->mm);

      free (chunk);
      return NULL;
    }

  curl_easy_setopt (curl, CURLOPT_URL, fl);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, __mrss_memorize_file);
  curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt (curl, CURLOPT_FILE, (void *) chunk);

  if (timeout > 0)
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeout);
  else if (timeout < 0)
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10);

  if (curl_easy_perform (curl))
    {
      if (chunk->mm)
	free (chunk->mm);

      free (chunk);

      curl_easy_cleanup (curl);
      return NULL;
    }

  curl_easy_cleanup (curl);

  return chunk;
}


__mrss_download_t * __mrss_download_file_auth(char *fl, int timeout, 
                                              char *userpass, 
					      long int authtype)
{
    __mrss_download_t *chunk;
    CURL           *curl;

    if (!(chunk = (__mrss_download_t *) malloc(sizeof(__mrss_download_t))))
        return NULL;

    chunk->mm = NULL;
    chunk->size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!(curl = curl_easy_init())) {
        if (chunk->mm)
            free(chunk->mm);

        free(chunk);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, fl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __mrss_memorize_file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_FILE, (void *) chunk);
    if( userpass ) {
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, authtype);
    }

    if (timeout > 0)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    else if (timeout < 0)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

    if (curl_easy_perform(curl)) {
        if (chunk->mm)
            free(chunk->mm);

        free(chunk);

        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);

    return chunk;
}


/* EOF */
