/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
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

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg );
char *botHelpTrout( void );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";


void plugin_initialize( char *args )
{
    static char    *command = "trout";

    printf( "Initializing trout...\n" );
    botCmd_add( (const char **)&command, botCmdTrout, botHelpTrout );
}

void plugin_shutdown( void )
{
    printf( "Removing trout...\n" );
    botCmd_remove( "trout" );
}


void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg )
{
    char           *message;
    char           *chan;
    bool            privmsg = false;
    int             len = 0;

    if( !channel ) {
        privmsg = true;
        message = strstr( msg, " " );
        if( message ) {
            len = message - msg;
            chan = strndup((const char *)msg, len);
            msg += (len + 1);
            while( *msg == ' ' ) {
                msg++;
            }
        } else {
            chan = msg;
            msg = NULL;
        }

        channel = FindChannel(server, chan);
        if( !channel ) {
            message = (char *)malloc(22 + len);
            sprintf( message, "Can't find channel %s", chan );
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                                  message);
            if( message ) {
                free( message );
            }

            if( chan != msg ) {
                free( chan );
            }
            return;
        }

        if( chan != msg ) {
            free( chan );
        }
    }

    if( !msg ) {
        message = (char *)malloc(29+strlen(who)+2);
        sprintf( message, "dumps a bucket of trout onto %s", who );
    } else {
        message = (char *)malloc(36+strlen(who)+strlen(msg)+2);
        sprintf( message, "slaps %s with a trout on behalf of %s...", msg, 
                 who );
    }
    LoggedActionMessage( server, channel, message );
    free(message);
}

char *botHelpTrout( void )
{
    static char *help = "Slaps someone with a trout on your behalf.  "
                        "Syntax: (in channel) trout nick  "
                        "(in privmsg) trout #channel nick.";
    
    return( help );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
