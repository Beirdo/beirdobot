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

#define CURRENT_SCHEMA_URL 1
#define MAX_SCHEMA_QUERY 100
typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_url_keywords` (\n"
    "    `serverid` INT NOT NULL ,\n"
    "    `chanid` INT NOT NULL ,\n"
    "    `keyword` VARCHAR( 255 ) NOT NULL ,\n"
    "    `url` VARCHAR( 255 ) NOT NULL ,\n"
    "    PRIMARY KEY ( `serverid`, `chanid`, `keyword` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_URL] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t urlQueryTable[] = {
    /* 0 */
    { "SELECT `url` FROM `plugin_url_keywords` WHERE `serverid` = ? AND "
      "`chanid` = ? AND `keyword` = ?", NULL, NULL, FALSE }
};

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdUrl( IRCServer_t *server, IRCChannel_t *channel, char *who,
                char *msg, void *tag );
char *botHelpUrl( void *tag );
static int db_upgrade_schema( int current, int goal );
char *db_get_url_keyword( IRCServer_t *server, IRCChannel_t *channel, 
                          char *keyword );
static void result_get_url_keyword( MYSQL_RES *res, MYSQL_BIND *input, 
                                    void *args );

void plugin_initialize( char *args )
{
    static char    *command = "url";
    char           *verString;
    int             ver;
    int             printed;

    LogPrintNoArg( LOG_NOTICE, "Initializing url plugin..." );

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting("dbSchemaUrl");
        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current URL database schema version %d", 
                                ver );
            LogPrint( LOG_CRIT, "Code supports version %d", 
                                CURRENT_SCHEMA_URL );
            printed = TRUE;
        }

        if( ver < CURRENT_SCHEMA_URL ) {
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA_URL );
        }
    } while( ver < CURRENT_SCHEMA_URL );

    botCmd_add( (const char **)&command, botCmdUrl, botHelpUrl, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing url plugin..." );
    botCmd_remove( "url" );
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

    keyword = CommandLineParse( msg, &msg );
    if( !keyword ) {
        message = strdup( "You need to specify the keyword!" );
    } else {
        urlFmt = db_get_url_keyword( server, channel, keyword );
        if( !urlFmt ) {
            message = (char *)malloc(22 + strlen(keyword));
            sprintf( message, "No match for keyword %s", keyword );
        } else {
            if( !msg ) {
                url = urlFmt;
            } else {
                expand = curl_escape( msg, 0 );
                len = strlen(urlFmt) + strlen(expand) + 10;
                url = (char *)malloc(len + 1);
                snprintf( url, len, urlFmt, expand );
                curl_free( expand );
                free( urlFmt );
            }

            len = strlen( url ) + strlen( keyword ) + 5;
            message = (char *)malloc(len + 1);
            snprintf( message, len, "%s: %s", keyword, url );
            free( url );
        }

        free( keyword );
    }

    if( privmsg ) {
        transmitMsg( server, TX_PRIVMSG, who, message);
    } else {
        LoggedChannelMessage(server, channel, message);
    }

    free( message );
}

char *botHelpUrl( void *tag )
{
    static char *help = "Expands a url from a keyword (configured per-channel)"
                        "  Syntax: (in channel) url keyword [expansion]  "
                        "(in privmsg) url #channel keyword [expansion].";

    return( help );
}

static int db_upgrade_schema( int current, int goal )
{
    int                 i;

    if( current >= goal ) {
        return( current );
    }

    if( current == 0 ) {
        /* There is no dbSchema, assume that it is an empty database, populate
         * with the default schema
         */
        LogPrint( LOG_ERR, "Initializing URL database to schema version %d",
                  CURRENT_SCHEMA_URL );
        for( i = 0; i < defSchemaCount; i++ ) {
            db_queue_query( i, defSchema, NULL, 0, NULL, NULL, NULL );
        }
        db_set_setting("dbSchemaUrl", "%d", CURRENT_SCHEMA_URL);
        return( CURRENT_SCHEMA_URL );
    }

    LogPrint( LOG_ERR, "Upgrading URL database from schema version %d to "
                       "%d", current, current+1 );
    for( i = 0; schemaUpgrade[current][i].queryPattern; i++ ) {
        db_queue_query( i, schemaUpgrade[current], NULL, 0, NULL, NULL, NULL );
    }

    current++;

    db_set_setting("dbSchemaUrl", "%d", current);
    return( current );
}

char *db_get_url_keyword( IRCServer_t *server, IRCChannel_t *channel, 
                          char *keyword )
{
    pthread_mutex_t        *mutex;
    MYSQL_BIND             *data;
    char                   *urlFmt;

    if( !keyword || !channel || !server ) {
        return( NULL );
    }

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    data = (MYSQL_BIND *)malloc(3 * sizeof(MYSQL_BIND));
    memset( data, 0, 3 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], server->serverId, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[2], keyword, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 0, urlQueryTable, data, 3, result_get_url_keyword,
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

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
