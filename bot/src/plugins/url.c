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
#include <curl/curl.h>

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#define CURRENT_SCHEMA_URL 3

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_url_keywords` (\n"
    "    `chanid` INT NOT NULL ,\n"
    "    `keyword` VARCHAR( 255 ) NOT NULL ,\n"
    "    `url` VARCHAR( 255 ) NOT NULL ,\n"
    "    `enabled` INT NOT NULL DEFAULT '1', \n"
    "    PRIMARY KEY ( `chanid`, `keyword` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE },
  { "CREATE TABLE `plugin_url_log` (\n"
    "    `urlId` INT NOT NULL AUTO_INCREMENT ,\n"
    "    `chanid` INT NOT NULL ,\n"
    "    `timestamp` INT NOT NULL ,\n"
    "    `url` TEXT NOT NULL ,\n"
    "    PRIMARY KEY ( `urlId` ) ,\n"
    "    INDEX `chanTime` ( `chanid` , `timestamp` ) ,\n"
    "    INDEX `url` ( `url` ( 256 ) ) \n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_URL] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } },
    /* 1 -> 2 */
    { { "ALTER TABLE `plugin_url_keywords` DROP `serverid`", NULL, NULL, 
        FALSE },
      { "CREATE TABLE `plugin_url_log` (\n"
        "    `urlId` INT NOT NULL AUTO_INCREMENT ,\n"
        "    `chanid` INT NOT NULL ,\n"
        "    `timestamp` INT NOT NULL ,\n"
        "    `url` TEXT NOT NULL ,\n"
        "    PRIMARY KEY ( `urlId` ) ,\n"
        "    INDEX `chanTime` ( `chanid` , `timestamp` ) ,\n"
        "    INDEX `url` ( `url` ( 256 ) ) \n"
        ") TYPE = MYISAM\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE } },
    /* 2 -> 3 */
    { { "ALTER TABLE `plugin_url_keywords` ADD `enabled` INT NOT NULL "
        "DEFAULT '1' AFTER `url`", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t urlQueryTable[] = {
    /* 0 */
    { "SELECT `url` FROM `plugin_url_keywords` WHERE `chanid` = ? AND "
      "`keyword` = ? AND `enabled` = 1", NULL, NULL, FALSE },
    /* 1 */
    { "INSERT INTO `plugin_url_log` (`chanid`, `timestamp`, `url`) "
      "VALUES ( ?, ?, ? )" },
    /* 2 */
    { "SELECT `timestamp`, `url` FROM `plugin_url_log` WHERE `chanid` = ? "
      "ORDER BY `timestamp` DESC LIMIT 3", NULL, NULL, FALSE },
    /* 3 */
    { "SELECT `timestamp`, `url` FROM `plugin_url_log` WHERE `chanid` = ? AND "
      "`url` LIKE ? ORDER BY `timestamp` DESC LIMIT 3", NULL, NULL, FALSE },
    /* 4 */
    { "SELECT `keyword`, `enabled` FROM `plugin_url_keywords` "
      "WHERE `chanid` = ? ORDER BY `keyword` ASC", NULL, NULL, FALSE }
};

typedef struct {
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    char               *who;
} URLArgs_t;

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdUrl( IRCServer_t *server, IRCChannel_t *channel, char *who,
                char *msg, void *tag );
char *botHelpUrl( void *tag );
void regexpFuncUrl( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag );
char *db_get_url_keyword( IRCChannel_t *channel, char *keyword );
static void result_get_url_keyword( MYSQL_RES *res, MYSQL_BIND *input, 
                                    void *args );
void db_log_url( IRCChannel_t *channel, time_t timestamp, char *url );
void db_last_url( IRCChannel_t *channel, char *who );
static void result_last_url( MYSQL_RES *res, MYSQL_BIND *input, void *args );
void db_search_url( IRCChannel_t *channel, char *who, char *text );
char *db_list_keywords( IRCChannel_t *channel );
static void result_list_keywords( MYSQL_RES *res, MYSQL_BIND *input, 
                                  void *args );

static char *urlRegexp = "(?i)(?:\\s|^)((?:https?|ftp)\\:\\/\\/\\S+)(?:\\s|$)";
int         urlMenuId;

void plugin_initialize( char *args )
{
    static char    *command = "url";
    static char     buf[32];

    LogPrintNoArg( LOG_NOTICE, "Initializing url plugin..." );

    db_check_schema( "dbSchemaUrl", "URL", CURRENT_SCHEMA_URL, defSchema,
                     defSchemaCount, schemaUpgrade );

    urlMenuId = cursesMenuItemAdd( 1, -1, "URL", NULL, NULL );

    snprintf( buf, 32, "%d.%d.%d", (LIBCURL_VERSION_NUM >> 16) & 0xFF,
                       (LIBCURL_VERSION_NUM >> 8) & 0xFF,
                       LIBCURL_VERSION_NUM & 0xFF );
    versionAdd( "CURL", buf );

    botCmd_add( (const char **)&command, botCmdUrl, botHelpUrl, NULL );
    regexp_add( NULL, (const char *)urlRegexp, regexpFuncUrl, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing url plugin..." );
    cursesMenuItemRemove( 1, urlMenuId, "URL" );
    regexp_remove( NULL, urlRegexp );
    botCmd_remove( "url" );
    versionRemove( "CURL" );

}


void botCmdUrl( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                char *msg, void *tag )
{
    char           *message;
    char           *chan;
    char           *keyword;
    bool            privmsg;
    char           *urlFmt;
    char           *url;
    char           *expand;
    int             len;
    CURL           *curl;

    if( !server || !msg ) {
        return;
    }

    if( !channel ) {
        privmsg = true;

        chan = CommandLineParse( msg, &msg );
        if( !msg ) {
            transmitMsg( server, TX_PRIVMSG, who,
                         "You must specify \"url #channel keyword "
                         "[expansion]\"" );
            return;
        }

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
        privmsg = FALSE;
    }

    message = NULL;
    keyword = CommandLineParse( msg, &msg );
    if( !keyword ) {
        message = strdup( "You need to specify the keyword!" );
    } else if( *keyword == '-' ) {
        free( keyword );

        keyword = CommandLineParse( msg, &msg );
        if( !keyword ) {
            message = strdup( "You need to specify \"list\", \"last\" or "
                              "\"search\"" );
        } else if( !strcasecmp( keyword, "last" ) ) {
            db_last_url( channel, who );
        } else if( !strcasecmp( keyword, "search" ) ) {
            db_search_url( channel, who, msg );
        } else if( !strcasecmp( keyword, "list" ) ) {
            message = db_list_keywords( channel );
        } else {
            message = strdup( "You need to specify \"list\", \"last\" or "
                              "\"search\"" );
        }
    } else {
        urlFmt = db_get_url_keyword( channel, keyword );
        if( !urlFmt ) {
            message = (char *)malloc(22 + strlen(keyword));
            sprintf( message, "No match for keyword %s", keyword );
        } else {
            if( !msg ) {
                url = urlFmt;
            } else {
#if ( LIBCURL_VERSION_NUM < 0x070f04 )
                (void)curl;
                expand = curl_escape( msg, 0 );
#else
                curl = curl_easy_init();
                expand = curl_easy_escape( curl, msg, 0 );
#endif
                len = strlen(urlFmt) + strlen(expand) + 10;
                url = (char *)malloc(len + 1);
                snprintf( url, len, urlFmt, expand );
                curl_free( expand );
#if ( LIBCURL_VERSION_NUM >= 0x070f04 )
                curl_easy_cleanup( curl );
#endif
                free( urlFmt );
            }

            len = strlen( url ) + strlen( keyword ) + 5;
            message = (char *)malloc(len + 1);
            snprintf( message, len, "%s: %s", keyword, url );
            free( url );
        }

        if( keyword ) {
            free( keyword );
        }
    }

    if( message ) {
        if( privmsg ) {
            transmitMsg( server, TX_PRIVMSG, who, message);
        } else {
            LoggedChannelMessage(server, channel, message);
        }

        free( message );
    }
}

char *botHelpUrl( void *tag )
{
    static char *help = "Expands a url from a keyword (configured per-channel)"
                        "  Syntax: (in channel) url keyword [expansion]  "
                        "(in privmsg) url #channel keyword [expansion].  Also "
                        "has url - list, url - last, "
                        "url - search searchstring.";

    return( help );
}

char *db_get_url_keyword( IRCChannel_t *channel, char *keyword )
{
    pthread_mutex_t        *mutex;
    MYSQL_BIND             *data;
    char                   *urlFmt;

    if( !keyword || !channel ) {
        return( NULL );
    }

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    data = (MYSQL_BIND *)malloc(2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], keyword, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 0, urlQueryTable, data, 2, result_get_url_keyword,
                    &urlFmt, mutex );

    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( urlFmt );
}


void result_get_url_keyword( MYSQL_RES *res, MYSQL_BIND *input, void *args )
{
    MYSQL_ROW       row;
    char          **pUrlFmt;
    char           *urlFmt;

    pUrlFmt = (char **)args;

    if( !res || !mysql_num_rows(res) ) {
        *pUrlFmt = NULL;
        return;
    }

    row = mysql_fetch_row(res);

    urlFmt = strdup( row[0] );
    *pUrlFmt = urlFmt;
}

void regexpFuncUrl( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag )
{
    struct timeval      now;
    char               *string;

    string = regexp_substring( msg, ovector, ovecsize, 1 );
    gettimeofday( &now, NULL );

    db_log_url( channel, now.tv_sec, string );

    free( string );
}

void db_log_url( IRCChannel_t *channel, time_t timestamp, char *url )
{
    MYSQL_BIND             *data;

    if( !url || !channel ) {
        return;
    }

    data = (MYSQL_BIND *)malloc(3 * sizeof(MYSQL_BIND));
    memset( data, 0, 3 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], timestamp, MYSQL_TYPE_LONG );
    bind_string( &data[2], url, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 1, urlQueryTable, data, 3, NULL, NULL, NULL );
}

void db_last_url( IRCChannel_t *channel, char *who )
{
    MYSQL_BIND     *data;
    URLArgs_t      *chanArgs;

    if( !who || !channel ) {
        return;
    }

    data = (MYSQL_BIND *)malloc(1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );

    chanArgs = (URLArgs_t *)malloc(sizeof(URLArgs_t));
    chanArgs->server  = channel->server;
    chanArgs->channel = channel;
    chanArgs->who     = who;

    db_queue_query( 2, urlQueryTable, data, 1, result_last_url, 
                   (void *)chanArgs, NULL );
}

static void result_last_url( MYSQL_RES *res, MYSQL_BIND *input, void *args )
{
    int             count;
    int             i;
    MYSQL_ROW       row;
    URLArgs_t      *chanArgs;
    static char    *none = "No matches found";
    time_t          timestamp;
    struct tm       tm;
    char            date[32];
    char            buf[1024];

    chanArgs = (URLArgs_t *)args;

    if( !res || !(count = mysql_num_rows(res)) ) {
        transmitMsg( chanArgs->server, TX_PRIVMSG, chanArgs->who, none );
        free( chanArgs );
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        timestamp = atol(row[0]);
        localtime_r(&timestamp, &tm);
        strftime( date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm );

        snprintf( buf, 1024, "%s: %s", date, row[1] );

        LogPrint( LOG_NOTICE, "URL: channel %d, %s", 
                              chanArgs->channel->channelId, buf );
        transmitMsg( chanArgs->server, TX_PRIVMSG, chanArgs->who, buf );
    }

    free( chanArgs );
}

void db_search_url( IRCChannel_t *channel, char *who, char *text )
{
    MYSQL_BIND     *data;
    URLArgs_t      *chanArgs;
    char           *like;

    if( !who || !channel ) {
        return;
    }

    if( !strchr( text, '%' ) ) {
        like = (char *)malloc(strlen(text) + 3);
        sprintf( like, "%%%s%%", text );
    } else {
        like = strdup( text );
    }

    data = (MYSQL_BIND *)malloc(2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], like, MYSQL_TYPE_VAR_STRING );

    chanArgs = (URLArgs_t *)malloc(sizeof(URLArgs_t));
    chanArgs->server  = channel->server;
    chanArgs->channel = channel;
    chanArgs->who     = who;

    db_queue_query( 3, urlQueryTable, data, 2, result_last_url, 
                    (void *)chanArgs, NULL );
}

char *db_list_keywords( IRCChannel_t *channel )
{
    pthread_mutex_t        *mutex;
    MYSQL_BIND             *data;
    char                   *keywords;

    if( !channel ) {
        return( NULL );
    }

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    data = (MYSQL_BIND *)malloc(1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );

    db_queue_query( 4, urlQueryTable, data, 1, result_list_keywords,
                    &keywords, mutex );

    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( keywords );
}

static void result_list_keywords( MYSQL_RES *res, MYSQL_BIND *input, 
                                  void *args )
{
    MYSQL_ROW       row;
    char          **pKeywords;
    char           *keywords;
    char           *key;
    int             count;
    int             i;
    int             len;
    bool            enabled;

    pKeywords = (char **)args;

    if( !res || !(count = mysql_num_rows(res)) ) {
        *pKeywords = NULL;
        return;
    }

    keywords = NULL;
    key = NULL;
    len = 0;

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        enabled = ( atoi(row[1]) == 0 ? FALSE : TRUE );
        len += strlen( row[0] ) + ( enabled ? 0 : 10 );
        keywords = (char *)realloc(keywords, len + 3);
        if( !key ) {
            key = keywords;
            *key = '\0';
        } else {
            len++;
            strcat( keywords, " " );
        }
        strcat( keywords, row[0] );
        if( !enabled ) {
            strcat( keywords, "(disabled)" );
        }
    }

    *pKeywords = keywords;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
