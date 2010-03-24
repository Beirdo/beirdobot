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
void botCmdTrout( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg, void *tag );
char *botHelpTrout( void *tag );
void botCmdSalmon( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag );
char *botHelpSalmon( void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    const char         *command;
    BotCmdFunc_t        func;
    BotCmdHelpFunc_t    helpFunc;
} FishCmd_t;

static FishCmd_t fishCmds[] = {
    { "trout", botCmdTrout, botHelpTrout },
    { "salmon", botCmdSalmon, botHelpSalmon },
    { NULL, NULL, NULL }
};

void plugin_initialize( char *args )
{
    FishCmd_t  *cmd;

    LogPrintNoArg( LOG_NOTICE, "Initializing trout..." );
    for( cmd = &fishCmds[0]; cmd && cmd->command; cmd++ ) {
        botCmd_add( &cmd->command, cmd->func, cmd->helpFunc,
                    (void *)cmd->command );
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

void botCmdSalmon( IRCServer_t *server, IRCChannel_t *channel, char *who, 
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
            transmitMsg( server, TX_PRIVMSG, who, "Try \"help salmon\"" );
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
        message = (char *)malloc(34+strlen(who)+2);
        sprintf( message, "throws several salmon at %s.  SPLAT!", who );
    } else {
        target = CommandLineParse( line, &line );
        if( line ) {
            len = 49 + strlen(who) + strlen(target) + strlen(line) + 2;
            message = (char *)malloc(len);
            sprintf( message, "swings a %s salmon at the head of %s on behalf of %s...", 
                     line, target, who );
        } else {
            len = 52 + strlen(who) + strlen(target) + 2;
            message = (char *)malloc(len);
            sprintf( message, "plants a salmon upside the head of %s on behalf of %s...", 
                     target, who );
        }
    }
    LoggedActionMessage( server, channel, message );
    free(target);
    free(message);
}

char *botHelpSalmon( void *tag )
{
    static char *help = "Abuses someone with a salmon on your behalf.  "
                        "Syntax: (in channel) salmon nick [adjective] "
                        "(in privmsg) salmon #channel nick [adjective]";
    
    return( help );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
