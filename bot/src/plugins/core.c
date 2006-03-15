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
#include <sys/time.h>
#include <time.h>
#include <mysql.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"
#include "logging.h"

char *db_quote(char *string);
MYSQL_RES *db_query( char *format, ... );

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who,
                   char *msg );
void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg );
char *botHelpSearch( void );
char *botHelpSeen( void );
char *botHelpNotice( void );
void db_search_text( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *text );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char    *commands[] = { "search", "seen", "notice" };

    LogPrintNoArg( LOG_NOTICE, "Initializing core plugin..." );
    botCmd_add( (const char **)&commands[0], botCmdSearch, botHelpSearch );
    botCmd_add( (const char **)&commands[1], botCmdSeen,   botHelpSeen );
    botCmd_add( (const char **)&commands[2], botCmdNotice, botHelpNotice );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing core plugin..." );
    botCmd_remove( "search" );
    botCmd_remove( "seen" );
    botCmd_remove( "notice" );
}


void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg )
{
    char           *message;
    char           *chan;
    bool            privmsg = false;
    int             len;
    struct timeval  start, end;

    if( !server || !msg ) {
        return;
    }

    if( !channel ) {
        privmsg = true;
        message = strstr( msg, " " );
        if( !message ) {
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                                  "You must specify \"search #channel text\"");
            return;
        }

        len = message - msg;
        chan = strndup(msg, len);
        chan[len] = '\0';

        msg += (len + 1);
        while( *msg == ' ' ) {
            msg++;
        }
        if( *msg == '\0' ) {
            msg = NULL;
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

    if( !strcmp( channel->url, "" ) ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                              "No URL configured" );
        return;
    }

    BN_SendPrivateMessage(&server->ircInfo, (const char *)who, "Searching..." );

    gettimeofday( &start, NULL );
    db_search_text( server, channel, who, msg );
    gettimeofday( &end, NULL );

    end.tv_sec  -= start.tv_sec;
    end.tv_usec -= start.tv_usec;
    while( end.tv_usec < 0 ) {
        end.tv_usec += 1000000;
        end.tv_sec  -= 1;
    }

    message = (char *)malloc(32);
    sprintf( message, "Search took %ld.%06lds", end.tv_sec, end.tv_usec );
    BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message);

    LogPrint( LOG_INFO, "Search for \"%s\" by %s in %s, duration %ld.%06lds", 
              msg, who, channel->fullspec, end.tv_sec, end.tv_usec );
    
    free( message );
}

char *botHelpSearch( void )
{
    static char *help = "Search for text in a channel's log, "
                        "returns top 3 matches by privmsg.  "
                        "Syntax: (in channel) search text  "
                        "(in privmsg) search #channel text.";

    return( help );
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
        chan[len] = '\0';

        msg += (len + 1);
        while( *msg == ' ' ) {
            msg++;
        }
        if( *msg == '\0' ) {
            msg = NULL;
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

#define SEARCH_WINDOW (15*60)
void db_search_text( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *text )
{
    int             count = 0;
    int             i;
    int             len;
    MYSQL_RES      *res;
    MYSQL_ROW       row;
    char           *value = NULL;
    static char    *none = "No matches found";
    time_t          time_start, time_end;
    struct tm       tm_start, tm_end;
    char            start[20], stop[20];
    float           score;
    char           *quotedText;

    if( !text || !channel || !server ) {
        return;
    }

    quotedText = db_quote(text);

    res = db_query( "SELECT "
                    "%d * FLOOR(MIN(`timestamp`) / %d) AS starttime, "
                    "SUM(MATCH(`nick`, `message`) AGAINST ('%s')) AS score "
                    "FROM `irclog` WHERE `msgtype` IN (0, 1) "
                    "AND MATCH(`nick`, `message`) AGAINST ('%s') > 0 "
                    "AND `chanid` = %d "
                    "GROUP BY %d * FLOOR(`timestamp` / %d) "
                    "ORDER BY score DESC, `msgid` ASC LIMIT 3", SEARCH_WINDOW,
                    SEARCH_WINDOW, quotedText, quotedText, channel->channelId,
                    SEARCH_WINDOW, SEARCH_WINDOW );

    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, none);
        return;
    }

    len = 16;
    value = (char *)realloc(value, len + 1);
    sprintf( value, "Top %d matches:", count );
    BN_SendPrivateMessage(&server->ircInfo, (const char *)who, value);

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        time_start = atoi(row[0]);
        time_end   = time_start + SEARCH_WINDOW;
        localtime_r(&time_start, &tm_start);
        localtime_r(&time_end, &tm_end);
        strftime( start, 20, "%Y-%m-%d:%H:%M", &tm_start );
        strftime( stop, 20, "%Y-%m-%d:%H:%M", &tm_end );
        score = atof(row[1]);

        len = strlen(channel->url) + 70;
        value = (char *)realloc(value, len + 1);
        sprintf( value, "    %s/%s/%s (%.2f)", channel->url, start, stop, 
                                               score );
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, value);
    }
    mysql_free_result(res);
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
