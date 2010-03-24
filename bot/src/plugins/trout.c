/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006, 2010 Gavin Hurlbut
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
* Copyright 2006, 2010 Gavin Hurlbut
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
void botCmdFish( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag );
char *botHelpFish( void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    const char         *command;
    const char         *fishSelf;
    const char         *fishOtherAdj;
    const char         *fishOther;
} FishCmd_t;

static FishCmd_t fishCmds[] = {
    { "trout", "dumps a bucket of trout onto %s", 
      "slaps %s with a %s trout on behalf of %s...",
      "slaps %s with a trout on behalf of %s..." },
    { "salmon", "throws several salmon at %s.  SPLAT!",
      "connects with the head of %s with a %s salmon on behalf of %s...",
      "plants a salmon upside the head of %s on behalf of %s..." },
    { NULL, NULL, NULL, NULL }
};

void plugin_initialize( char *args )
{
    FishCmd_t  *cmd;

    LogPrintNoArg( LOG_NOTICE, "Initializing trout..." );
    for( cmd = &fishCmds[0]; cmd && cmd->command; cmd++ ) {
        botCmd_add( &cmd->command, botCmdFish, botHelpFish, (void *)cmd );
    }
}

void plugin_shutdown( void )
{
    FishCmd_t  *cmd;

    LogPrintNoArg( LOG_NOTICE, "Removing trout..." );
    for( cmd = &fishCmds[0]; cmd && cmd->command; cmd++ ) {
        botCmd_remove( (char *)cmd->command );
    }
}


void botCmdFish( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag )
{
    FishCmd_t      *cmd;
    char           *message;
    char           *line;
    char           *target;
    char           *chan;
    int             len;
    bool            privmsg = false;

    cmd = (FishCmd_t *)tag;
    if( !cmd ) {
        return;
    }

    if( !channel ) {
        privmsg = true;
        if( !msg ) {
            message = (char *)malloc(12 + strlen(cmd->command));
            sprintf(message, "Try \"help %s\"", cmd->command);
            transmitMsg( server, TX_PRIVMSG, who, message );
            if( message ) {
                free( message );
            }
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
        len = strlen(cmd->fishSelf) + strlen(who) + 2;
        message = (char *)malloc(len);
        snprintf( message, len-1, cmd->fishSelf, who );
    } else {
        target = CommandLineParse( line, &line );
        if( line ) {
            len = strlen(cmd->fishOtherAdj) + strlen(who) + strlen(target) + 
                  strlen(line) + 2;
            message = (char *)malloc(len);
            snprintf( message, len-1, cmd->fishOtherAdj, target, line, who );
        } else {
            len = strlen(cmd->fishOther) + strlen(who) + strlen(target) + 2;
            message = (char *)malloc(len);
            snprintf( message, len-1, cmd->fishOther, target, who );
        }
    }
    LoggedActionMessage( server, channel, message );

    if( target ) {
        free(target);
    }

    if( message ) {
        free(message);
    }
}

char *botHelpFish( void *tag )
{
    FishCmd_t      *cmd;
    static char    *helpFmt = "Abuses someone with a %s on your behalf.  "
                           "Syntax: (in channel) %s nick [adjective] "
                           "(in privmsg) %s #channel nick [adjective]";
    static char    *badFish = "Unknown help for unknown abuse fish!";
    static char    *help = NULL;
    int             len;

    cmd = (FishCmd_t *)tag;
    if( !cmd ) {
        return( badFish );
    }

    len = strlen( helpFmt ) + (3 * strlen( cmd->command )) + 2;

    help = (char *)realloc(help, len);
    sprintf( help, helpFmt, cmd->command, cmd->command, cmd->command );
    
    return( help );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
