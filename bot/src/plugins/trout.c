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
#include "environment.h"
#include "botnet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "structs.h"
#include "protos.h"
#include "logging.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg, void *tag );
char *botHelpTrout( void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";


void plugin_initialize( char *args )
{
    static char    *command = "trout";

    LogPrintNoArg( LOG_NOTICE, "Initializing trout..." );
    botCmd_add( (const char **)&command, botCmdTrout, botHelpTrout, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing trout..." );
    botCmd_remove( "trout" );
}


void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg, void *tag )
{
    char           *message;
    char           *line;
    char           *target;
    char           *chan;
    int             len;
    bool            privmsg = false;

    if( !channel ) {
        privmsg = true;
        if( !msg ) {
            transmitMsg( server, TX_PRIVMSG, who, "Try \"help trout\"" );
            return;
        }

        chan = CommandLineParse( msg, &line );

        channel = FindChannel(server, chan);
        if( !channel ) {
            message = (char *)malloc(22 + strlen(chan));
            sprintf( message, "Can't find channel %s", chan );
            transmitMsg( server, TX_PRIVMSG, who, message);
            if( message ) {
                free( message );
            }

            if( chan ) {
                free( chan );
            }
            return;
        }

        if( chan ) {
            free( chan );
        }
    } else {
        line = msg;
    }

    if( !msg ) {
        target = NULL;
        message = (char *)malloc(29+strlen(who)+2);
        sprintf( message, "dumps a bucket of trout onto %s", who );
    } else {
        target = CommandLineParse( line, &line );
        if( line ) {
            len = 38 + strlen(who) + strlen(target) + strlen(line) + 2;
            message = (char *)malloc(len);
            sprintf( message, "slaps %s with a %s trout on behalf of %s...", 
                     target, line, who );
        } else {
            len = 36 + strlen(who) + strlen(target) + 2;
            message = (char *)malloc(len);
            sprintf( message, "slaps %s with a trout on behalf of %s...", 
                     target, who );
        }
    }
    LoggedActionMessage( server, channel, message );
    free(target);
    free(message);
}

char *botHelpTrout( void *tag )
{
    static char *help = "Slaps someone with a trout on your behalf.  "
                        "Syntax: (in channel) trout nick [adjective] "
                        "(in privmsg) trout #channel nick [adjective]";
    
    return( help );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
