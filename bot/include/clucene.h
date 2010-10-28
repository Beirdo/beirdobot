/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2010 Gavin Hurlbut
 *
 *  beirdobot is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2010 Gavin Hurlbut
* All rights reserved
*
*/

#ifndef clucene_h_
#define clucene_h_

#include "environment.h"

/* CVS generated ID string (optional for h files) */
static char clucene_h_ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    unsigned long   timestamp;
    float           score;
} SearchResults_t;


#ifdef __cplusplus
extern "C" {
#endif

#define SEARCH_WINDOW (15*60)

/* C interface portion */
void clucene_shutdown(void);
#ifndef WEBSERVICE
void clucene_init(int clear);
void clucene_add( int chanid, char *nick, char *text, 
                  unsigned long timestamp );
#endif
SearchResults_t *clucene_search( int chanid, char *text, uint32 *count, 
                                 uint32 max );

#ifdef __cplusplus
}

/* C++ internals portion */

#endif

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
