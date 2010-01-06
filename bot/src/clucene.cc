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

#include "environment.h"
#include "clucene.h"
#include <CLucene.h>


static char ident[] _UNUSED_= 
    "$Id$";

/* The C interface portion */
extern "C" {
    void clucene_init(void)
    {
    }

    void clucene_shutdown(void)
    {
    }

    void clucene_add( unsigned long id, int chanid, char *nick, 
                      int msgType, char *text, unsigned long timestamp )
    {
    }
}

/* C++ internals */

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
