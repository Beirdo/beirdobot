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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botnet.h"
#include "environment.h"
#include "structs.h"
#include "protos.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";


void plugin_initialize( char *args )
{
    static char    *command = "trout";

    printf( "Initializing trout...\n" );
    botCmd_add( (const char **)&command, botCmdTrout );
}

void plugin_shutdown( void )
{
    printf( "Removing trout...\n" );
    botCmd_remove( "trout" );
}


void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg )
{
    char       *message;

    if( !msg ) {
        printf( "Bot CMD: Trout NULL by %s\n", who );
    } else {
        printf( "Bot CMD: Trout %s by %s\n", msg, who );
    }

    if( !channel ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who,
                              "You need to do this in a public channel!");
        return;
    }

    if( !msg ) {
        message = (char *)malloc(29+strlen(who)+2);
        sprintf( message, "dumps a bucket of trout onto %s", who );
    } else {
        message = (char *)malloc(36+strlen(who)+strlen(msg)+2);
        sprintf( message, "slaps %s with a trout on behalf of %s...", msg, 
                 who );
    }
    BN_SendActionMessage( &server->ircInfo, (const char *)channel->channel,
                          (const char *)message );
    free(message);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
