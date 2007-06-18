/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2007 Gavin Hurlbut
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
* Copyright 2007 Gavin Hurlbut
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
void regexpFuncTicket( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                       char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                       void *tag );
void regexpFuncChangeset( IRCServer_t *server, IRCChannel_t *channel, 
                          char *who, char *msg, IRCMsgType_t type, 
                          int *ovector, int ovecsize, void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static char    *ticketRegexp = "(?i)(?:\\s|^)\\#(\\d+)(?:\\s|$)";
static char    *changesetRegexp = "(?i)(?:\\s|^)\\[(\\d+)\\](?:\\s|$)";

void plugin_initialize( char *args )
{
    LogPrintNoArg( LOG_NOTICE, "Initializing trac..." );
    regexp_add( NULL, (const char *)ticketRegexp, regexpFuncTicket, NULL );
    regexp_add( NULL, (const char *)changesetRegexp, regexpFuncChangeset, 
                NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing trac..." );
    regexp_remove( NULL, ticketRegexp );
    regexp_remove( NULL, changesetRegexp );
}

void regexpFuncTicket( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                       char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                       void *tag )
{
    char       *string;
    char       *message;

    string = regexp_substring( msg, ovector, ovecsize, 1 );
    if( string ) {
        message = (char *)malloc(23 + strlen(string));
        sprintf( message, "Trac: Ticket %s detected", string );
        LogPrint( LOG_DEBUG, "%s in %s", message, channel->fullspec );
        LoggedChannelMessage( server, channel, message );
        free( message );
        free( string );
    }
}

void regexpFuncChangeset( IRCServer_t *server, IRCChannel_t *channel, 
                          char *who, char *msg, IRCMsgType_t type, 
                          int *ovector, int ovecsize, void *tag )
{
    char       *string;
    char       *message;

    string = regexp_substring( msg, ovector, ovecsize, 1 );
    if( string ) {
        message = (char *)malloc(23 + strlen(string));
        sprintf( message, "Trac: Changeset %s detected", string );
        LogPrint( LOG_DEBUG, "%s in %s", message, channel->fullspec );
        LoggedChannelMessage( server, channel, message );
        free( message );
        free( string );
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
