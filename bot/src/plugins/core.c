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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who,
                   char *msg );
void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg );
char *botHelpSeen( void );
char *botHelpNotice( void );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char    *commands[] = { "search", "seen", "notice" };

    printf( "Initializing core plugin...\n" );
    botCmd_add( (const char **)&commands[0], botCmdSearch, NULL );
    botCmd_add( (const char **)&commands[1], botCmdSeen,   botHelpSeen );
    botCmd_add( (const char **)&commands[2], botCmdNotice, botHelpNotice );
}

void plugin_shutdown( void )
{
    printf( "Removing core plugin...\n" );
    botCmd_remove( "search" );
    botCmd_remove( "seen" );
    botCmd_remove( "notice" );
}


void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg )
{
    if( !msg ) {
        printf( "Bot CMD: Search NULL by %s\n", who );
    } else {
        printf( "Bot CMD: Search %s by %s\n", msg, who );
    }
}

void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg )
{
    char           *message;
    static char    *huh = "Huh? Who?";
    char           *chan;
    bool            privmsg = false;
    int             len;

    if( !server || !msg ) {
        return;
    }

    if( !channel ) {
        privmsg = true;
        message = strstr( msg, " " );
        if( !message ) {
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                                  "You must specify \"seen #channel nick\"");
            return;
        }

        len = message - msg;
        chan = strndup(msg, len);
        msg += (len + 1);
        while( *msg == ' ' ) {
            msg++;
        }

        channel = FindChannel(server, chan);
        if( !channel ) {
            message = (char *)malloc(22 + len);
            sprintf( message, "Can't find channel %s", chan );
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                                  message);
            free( message );
            free( chan );
            return;
        }
        free( chan );
    }

    if( !msg ) {
        message = huh;
    } else {
        message = db_get_seen( channel, msg, privmsg );
    }

    if( privmsg ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message);
    } else {
        LoggedChannelMessage(server, channel, message);
    }
    
    if( message != huh ) {
        free( message );
    }
}

char *botHelpSeen( void )
{
    static char *help = "Shows when the last time a user has been seen, or how"
                        " long they've been idle.  "
                        "Syntax: (in channel) seen nick  "
                        "(in privmsg) seen #channel nick.";

    return( help );
}

void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg )
{
    char           *message;
    bool            privmsg = false;

    if( !server ) {
        return;
    }

    message = (char *)malloc(MAX_STRING_LENGTH);
    if( !channel ) {
        privmsg = true;
        channel = FindChannel(server, msg);
        if( !channel ) {
            snprintf( message, MAX_STRING_LENGTH, "Can't find channel %s", 
                      msg );
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                                  message );
            free( message );
            return;
        }
    }

    if( strcmp( channel->url, "" ) ) {
        snprintf( message, MAX_STRING_LENGTH, 
                  "This channel (%s) is logged -- %s", channel->channel, 
                  channel->url );
    } else {
        snprintf( message, MAX_STRING_LENGTH,
                  "This channel (%s) has no configured URL for logs",
                  channel->channel );
    }

    if( privmsg ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message );
    } else {
        LoggedChannelMessage(server, channel, message);
    }
    
    free( message );
}

char *botHelpNotice( void )
{
    static char *help = "Shows the channel's notice which includes the URL to "
                        " the logs online.  "
                        "Syntax: (in channel) notice  "
                        "(in privmsg) notice #channel";

    return( help );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
