/* mRss - Copyright (C) 2005-2006 bakunin - Andrea Marchesini 
 *                                    <bakunin@autistici.org>
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Public License as published 
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This source code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Please refer to the GNU Public License for more details.
 *
 * You should have received a copy of the GNU Public License along with
 * this source code; if not, write to:
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __M_RSS_INTERNAL_H__
#define __M_RSS_INTERNAL_H__

#include <curl/curl.h>
#include <nxml.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

typedef struct __mrss_download_t__ __mrss_download_t;

/**
 * \brief
 * For internal use only
 */
struct __mrss_download_t__
{
  char *mm;
  size_t size;
};

__mrss_download_t *	__mrss_download_file	(char *, int timeout);
__mrss_download_t * __mrss_download_file_auth(char *fl, int timeout, 
                                              char *userpass, 
					      long int authtype);

#endif

/* EOF */

