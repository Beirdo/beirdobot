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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <mysql.h>
#include <execinfo.h>
#include <stdint.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"
#include "logging.h"
#include "clucene.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who,
                   char *msg, void *tag );
void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag );
void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag );
void botCmdSymbol( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag );
char *botHelpSearch( void *tag );
char *botHelpSeen( void *tag );
char *botHelpNotice( void *tag );
char *botHelpSymbol( void *tag );
void lucene_search_text( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                         char *text );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char    *commands[] = { "search", "seen", "notice", "symbol" };

    LogPrintNoArg( LOG_NOTICE, "Initializing core plugin..." );
    botCmd_add( (const char **)&commands[0], botCmdSearch, botHelpSearch, 
                NULL );
    botCmd_add( (const char **)&commands[1], botCmdSeen,   botHelpSeen, 
                NULL );
    botCmd_add( (const char **)&commands[2], botCmdNotice, botHelpNotice,
                NULL );
    botCmd_add( (const char **)&commands[3], botCmdSymbol, botHelpSymbol,
                NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing core plugin..." );
    botCmd_remove( "search" );
    botCmd_remove( "seen" );
    botCmd_remove( "notice" );
    botCmd_remove( "symbol" );
}


void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag )
{
    char           *message;
    char           *search;
    char           *chan;
    bool            privmsg = false;
    struct timeval  start, end;

    if( !server || !msg ) {
        return;
    }

    if( !channel ) {
        privmsg = true;

        chan = CommandLineParse( msg, &message );
        if( !message ) {
            transmitMsg( server, TX_PRIVMSG, who,
                         "You must specify \"search #channel text\"");
            return;
        }

        search = message;

        channel = FindChannel(server, chan);
        if( !channel ) {
            message = (char *)malloc(22 + strlen(chan));
            sprintf( message, "Can't find channel %s", chan );
            transmitMsg( server, TX_PRIVMSG, who, message);
            free( message );
            free( chan );
            return;
        }
        free( chan );
    } else {
        search = msg;
    }

    if( !strcmp( channel->url, "" ) ) {
        transmitMsg( server, TX_PRIVMSG, who, "No URL configured" );
        return;
    }


    message = (char *)malloc(32 + strlen(search) + strlen(channel->channel) );
    sprintf( message, "Searching %s for \"%s\"...", channel->channel, search );
    transmitMsg( server, TX_PRIVMSG, who, message );

    gettimeofday( &start, NULL );
    lucene_search_text( server, channel, who, search );
    gettimeofday( &end, NULL );

    end.tv_sec  -= start.tv_sec;
    end.tv_usec -= start.tv_usec;
    while( end.tv_usec < 0 ) {
        end.tv_usec += 1000000;
        end.tv_sec  -= 1;
    }

    sprintf( message, "Search took %ld.%06lds", end.tv_sec, end.tv_usec );
    transmitMsg( server, TX_PRIVMSG, who, message);

    LogPrint( LOG_INFO, "Search for \"%s\" by %s in %s, duration %ld.%06lds", 
              msg, who, channel->fullspec, end.tv_sec, end.tv_usec );
    
    free( message );
}

char *botHelpSearch( void *tag )
{
    static char *help = "Search for text in a channel's log, "
                        "returns top 3 matches by privmsg.  "
                        "Syntax: (in channel) search text  "
                        "(in privmsg) search #channel text.";

    return( help );
}

void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag )
{
    char           *message;
    static char    *huh = "Huh? Who?";
    char           *chan;
    bool            privmsg = false;

    if( !server || !msg ) {
        return;
    }

    if( !channel ) {
        privmsg = true;

        chan = CommandLineParse( msg, &message );
        if( !message ) {
            transmitMsg( server, TX_PRIVMSG, who, 
                         "You must specify \"seen #channel nick\"");
            return;
        }
        msg = message;

        channel = FindChannel(server, chan);
        if( !channel ) {
            message = (char *)malloc(22 + strlen(chan));
            sprintf( message, "Can't find channel %s", chan );
            transmitMsg( server, TX_PRIVMSG, who, message);
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
        transmitMsg( server, TX_PRIVMSG, who, message);
    } else {
        LoggedChannelMessage(server, channel, message);
    }
    
    if( message != huh ) {
        free( message );
    }
}

char *botHelpSeen( void *tag )
{
    static char *help = "Shows when the last time a user has been seen, or how"
                        " long they've been idle.  "
                        "Syntax: (in channel) seen nick  "
                        "(in privmsg) seen #channel nick.";

    return( help );
}

void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag )
{
    char           *message;
    bool            privmsg = false;

    if( !server ) {
        return;
    }

    message = (char *)malloc(MAX_STRING_LENGTH);
    if( !channel ) {
        privmsg = true;

        if( !msg ) {
            transmitMsg( server, TX_PRIVMSG, who, "Try \"help notice\"" );
            return;
        }

        channel = FindChannel(server, msg);
        if( !channel ) {
            snprintf( message, MAX_STRING_LENGTH, "Can't find channel %s", 
                      msg );
            transmitMsg( server, TX_PRIVMSG, who, message );
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
        transmitMsg( server, TX_PRIVMSG, who, message );
    } else {
        LoggedChannelMessage(server, channel, message);
    }
    
    free( message );
}

char *botHelpNotice( void *tag )
{
    static char *help = "Shows the channel's notice which includes the URL to "
                        " the logs online.  "
                        "Syntax: (in channel) notice  "
                        "(in privmsg) notice #channel";

    return( help );
}

void botCmdSymbol( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag )
{
    uintptr_t       addr;
    void           *array[1];
    char          **strings;

    if( !server || channel ) {
        return;
    }

    if( !msg ) {
        transmitMsg( server, TX_PRIVMSG, who, "Try \"help symbol\"" );
        return;
    }

    addr = strtol(msg, NULL, 16);

    array[0] = (void *)addr;
    strings = backtrace_symbols( array, 1 );

    transmitMsg( server, TX_PRIVMSG, who, strings[0] );
    free( strings );
}

char *botHelpSymbol( void *tag )
{
    static char *help = "Looks up a symbol by address.  Privmsg only.";

    return( help );
}

void lucene_search_text( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                         char *text )
{
    SearchResults_t    *results;
    int                 count;
    int                 i;
    int                 len;
    char               *value = NULL;
    static char        *none = "No matches found";
    time_t              time_start, time_end;
    struct tm           tm_start, tm_end;
    char                start[20], stop[20];
    float               score;

    if( !text || !channel || !server ) {
        return;
    }

    LogPrintNoArg( LOG_INFO, "about to call clucene_search" );
    results = clucene_search( channel->channelId, text, &count );
    LogPrintNoArg( LOG_INFO, "returned from clucene_search" );

    if( !results ) {
        transmitMsg( server, TX_PRIVMSG, who, none );
        return;
    }

    len = 16;
    value = (char *)realloc(value, len + 1);
    sprintf( value, "Top %d matches:", count );
    transmitMsg( server, TX_PRIVMSG, who, value );

    for( i = 0; i < count; i++ ) {
        time_start = results[i].timestamp;
        time_end   = time_start + SEARCH_WINDOW;
        localtime_r(&time_start, &tm_start);
        localtime_r(&time_end, &tm_end);
        strftime( start, 20, "%Y-%m-%d:%H:%M", &tm_start );
        strftime( stop, 20, "%Y-%m-%d:%H:%M", &tm_end );
        score = results[i].score;

        len = strlen(channel->url) + 70;
        value = (char *)realloc(value, len + 1);
        sprintf( value, "    %s/%s/%s (%.2f)", channel->url, start, stop, 
                                               score );
        transmitMsg( server, TX_PRIVMSG, who, value);
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
