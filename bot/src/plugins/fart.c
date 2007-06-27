/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  This plugin is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This plugin is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this plugin; if not, write to the 
 *    Free Software Foundation, Inc., 
 *    51 Franklin Street, Fifth Floor, 
 *    Boston, MA  02110-1301  USA
 */

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2006 Gavin Hurlbut
* All rights reserved
*/

/* INCLUDE FILES */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botnet.h"
#include "environment.h"
#include "structs.h"
#include "protos.h"
#include "logging.h"

/* INTERNAL FUNCTION PROTOTYPES */
void regexpFuncFart( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                     void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static char    *contentRegexp = "(?i)(\\B|\\s|^)farts?(\\s|\\.|$)";

void plugin_initialize( char *args )
{
    LogPrintNoArg( LOG_NOTICE, "Initializing fart..." );
    regexp_add( NULL, (const char *)contentRegexp, regexpFuncFart, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing fart..." );
    regexp_remove( NULL, contentRegexp );
}

void regexpFuncFart( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                     void *tag )
{
    char       *message;

    if( !channel ) {
        return;
    }

    message = (char *)malloc(14+strlen(who)+2);
    sprintf( message, "farts back at %s", who );
    LoggedActionMessage( server, channel, message );
    free(message);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
