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

#ifdef __cplusplus
extern "C" {
#endif

/* C interface portion */
void clucene_init(void);
void clucene_shutdown(void);
void clucene_add( unsigned long id, int chanid, char *nick, int msgType, 
                  char *text, unsigned long timestamp );

#ifdef __cplusplus
}

/* C++ internals portion */

#endif

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
