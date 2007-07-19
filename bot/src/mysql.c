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
 *
 * Handles MySQL database connections
 */

#include "environment.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <mysql.h>
#include <mysql/errmsg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"
#include "protected_data.h"
#include "logging.h"
#include "queue.h"

static char ident[] _UNUSED_ =
    "$Id$";

/* 
 * 1h activity timeout for MySQL, ping the server if idle that long 
 * The code should check against the results of "SELECT @@wait_timeout" from
 * the server to make sure that we are delaying less time than the server is.
 * */
#define MYSQL_PING_THRESHOLD    (60 * 60)

typedef struct {
    MYSQL  *sql;
    char    sqlbuf[MAX_STRING_LENGTH];
    int     buflen;
} MysqlData_t;

static ProtectedData_t *sql;
pthread_t       sqlThreadId;

/* Internal protos */
char *db_quote(char *string);
MYSQL_RES *db_query( const char *query, MYSQL_BIND *args, int arg_count, 
                     bool *connected, long *insertid );
bool db_server_connect( MYSQL *sql );

void serverTreeLoadChannels( BalancedBTreeItem_t *node, 
                             pthread_mutex_t *mutex );

void chain_update_nick( MYSQL_RES *res, QueryItem_t *item );
void chain_flush_nick( MYSQL_RES *res, QueryItem_t *item );
void chain_set_setting( MYSQL_RES *res, QueryItem_t *item );
void chain_set_auth( MYSQL_RES *res, QueryItem_t *item );

void result_load_servers( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                          long insertid );
void result_load_channels( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                           long insertid );
void result_insert_server( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                           long insertid );
void result_insert_channel( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                            long insertid );
void result_check_nick_notify( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                               long insertid );
void result_get_plugins( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                         long insertid );
void result_get_seen( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                      long insertid );
void result_get_setting( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                         long insertid );
void result_get_auth( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                      long insertid );
void result_check_plugins( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                          long insertid );
void result_MySQL_Schema( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                          long insertid );

void cursesMySQLSchema( void *arg );

QueryTable_t    QueryTable[] = {
    /* 0 */
    { "SELECT `serverid`, `server`, `port`, `password`, `nick`, `username`, "
      "`realname`, `nickserv`, `nickservmsg`, `floodInterval`, `floodMaxTime`, "
      "`floodBuffer`, `floodMaxLine`, `enabled` FROM `servers` "
      "ORDER BY `serverid`", NULL, NULL, FALSE },
    /* 1 */
    { "SELECT `chanid`, `channel`, `url`, `notifywindow`, `cmdChar`, `enabled` "
      "FROM `channels` WHERE `serverid` = ? ORDER BY `chanid`", 
      NULL, NULL, FALSE },
    /* 2 */
    { "INSERT INTO `irclog` (`chanid`, `timestamp`, `nick`, `msgtype`, "
      "`message`) VALUES ( ?, ?, ?, ?, ? )", NULL, NULL, FALSE },
    /* 3 */
    { "SELECT * FROM `nicks` WHERE `chanid` = ? AND `nick` = ?", 
      chain_update_nick, NULL, FALSE },
    /* 4 */
    { "UPDATE `nicks` SET `lastseen` = ?, `present` = ? WHERE `chanid` = ? "
       "AND `nick` = ?", NULL, NULL, FALSE },
    /* 5 */
    { "INSERT INTO `nicks` (`lastseen`, `present`, `chanid`, `nick`, "
      "`lastnotice`) VALUES ( ?, ?, ?, ?, 0 )", NULL, NULL, FALSE },
    /* 6 */
    { "UPDATE `nicks` SET `present` = 0 WHERE `chanid` = ?", NULL, NULL, 
      FALSE },
    /* 7 */
    { "SELECT DISTINCT `nicks`.`chanid` FROM `nicks`, `channels` "
      "WHERE `nicks`.`chanid` = `channels`.`chanid` AND "
      "`channels`.`serverid` = ? AND `nicks`.`nick` = ?", chain_flush_nick,
      NULL, FALSE },
    /* 8 */
    { "SELECT `lastnotice` FROM `nicks` WHERE `chanid` = ? AND `nick` = ? AND "
      "`lastnotice` <= ?", NULL, NULL, FALSE },
    /* 9 */
    { "INSERT INTO `nickhistory` ( `chanid`, `nick`, `histType`, `timestamp` )"
      " VALUES ( ?, ?, ?, ? )", NULL, NULL, FALSE },
    /* 10 */
    { "UPDATE `nicks` SET `lastnotice` = ? WHERE `chanid` = ? AND `nick` = ?",
      NULL, NULL, FALSE },
    /* 11 */
    { "SELECT `pluginName`, `libName`, `preload`, `arguments` FROM `plugins`",
      NULL, NULL, FALSE },
    /* 12 */
    { "SELECT UNIX_TIMESTAMP(NOW()) - `lastseen`, `present` "
      "FROM `nicks` WHERE `chanid` = ? AND `nick` = ?", NULL, NULL, FALSE },
    /* 13 */
    { "SELECT `value` FROM `settings` WHERE `name` = ? LIMIT 1", NULL, NULL, 
      FALSE },
    /* 14 */
    { "SELECT `value` FROM `settings` WHERE `name` = ? LIMIT 1", 
      chain_set_setting, NULL, FALSE },
    /* 15 */
    { "UPDATE `settings` SET `value` = ? WHERE `name` = ?", NULL, NULL, FALSE },
    /* 16 */
    { "INSERT INTO `settings` (`name`, `value`) VALUES ( ?, ? )", NULL, NULL,
      FALSE },
    /* 17 */
    { "SELECT `username`, `digest`, `seed`, `key`, `keyIndex` FROM `userauth` "
      "WHERE `username` = ? LIMIT 1", NULL, NULL, FALSE },
    /* 18 */
    { "SELECT * FROM `userauth` WHERE `username` = ? LIMIT 1", chain_set_auth,
      NULL, FALSE },
    /* 19 */
    { "UPDATE `userauth` SET `digest` = ?, `seed` = ?, `key` = ?, "
      "`keyIndex` = ? WHERE `username` = ?", NULL, NULL, FALSE },
    /* 20 */
    { "INSERT INTO `userauth` (`username`, `digest`, `seed`, `key`, "
      "`keyIndex`) VALUES ( ?, ?, ?, ?, ? )", NULL, NULL, FALSE },
    /* 21 */
    { "SELECT `pluginName` FROM `plugins` WHERE `pluginName` = ?", NULL, NULL,
      FALSE },
    /* 22 */
    { "INSERT INTO `plugins` ( `pluginName`, `libName`, `preload`, "
      "`arguments`) VALUES ( ?, ?, ?, ? )", NULL, NULL, FALSE },
    /* 23 */
    { "SELECT `name`, `value` FROM `settings` ORDER BY `name` ASC", NULL, NULL,
      FALSE },
    /* 24 */
    { "UPDATE `channels` SET `serverid` = ?, `channel` = ?, "
      "`url` = ?, `notifywindow` = ?, `cmdChar` = ?, `enabled` = ? "
      "WHERE `chanid` = ?", NULL, NULL, FALSE },
    /* 25 */
    { "UPDATE `servers` SET `server` = ?, `port` = ?, `password` = ?, "
      "`nick` = ?, `username` = ?, `realname` = ?, `nickserv` = ?, "
      "`nickservmsg` = ?, `floodInterval` = ?, `floodMaxTime` = ?, "
      "`floodBuffer` = ?, `floodMaxLine` = ?, `enabled` = ? "
      "WHERE `serverid` = ? ", NULL, NULL, FALSE },
    /* 26 */
    { "INSERT INTO `channels` ( `serverid`, `channel`, `url`, `notifywindow`, "
      "`cmdChar`, `enabled`) VALUES ( ?, ?, ?, ?, ?, ? )", NULL, NULL, FALSE },
    /* 27 */
    { "INSERT INTO `servers` ( `server`, `port`, `password`, `nick`, "
      "`username`, `realname`, `nickserv`, `nickservmsg`, `floodInterval`, "
      "`floodMaxTime`, `floodBuffer`, `floodMaxLine`, `enabled` ) "
      "VALUES ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )", NULL, NULL, FALSE }
};

QueueObject_t   *QueryQ;

void *mysql_thread( void *arg ) 
{
    QueryItem_t        *item;
    MysqlData_t        *protItem;
    QueryTable_t       *query = NULL;
    MYSQL_RES          *res = NULL;
    int                 i;
    time_t              lastAccess;
    struct timeval      now;
    int                 timeout;
    bool                connected;
    long                insertid;

    LogPrintNoArg( LOG_NOTICE, "Starting MySQL thread" );
    mysql_thread_init();

    cursesMenuItemAdd( 2, MENU_SYSTEM, "Database Schema", cursesMySQLSchema,
                       NULL );

    gettimeofday( &now, NULL );
    lastAccess = now.tv_sec;

    while( !BotDone ) {
        item = (QueryItem_t *)QueueDequeueItem( QueryQ, 1000 );
        if( !item ) {
            continue;
        }

        connected = TRUE;

        do {
            gettimeofday( &now, NULL );
            timeout = now.tv_sec - lastAccess;
            lastAccess = now.tv_sec;

            if( timeout >= MYSQL_PING_THRESHOLD ) {
                LogPrint( LOG_NOTICE, "MySQL session idle for %ds, pinging",
                                      timeout );
                connected = FALSE;
            }

            if( !connected ) {
                /* Ping the server, if it's gone, reconnect */
                ProtectedDataLock( sql );
                protItem = (MysqlData_t *)sql->data;
                while( !BotDone && !connected ) {
                    connected = (mysql_ping( protItem->sql ) != 0 ? FALSE : 
                                 TRUE);
                    if( !connected ) {
                        LogPrintNoArg( LOG_NOTICE, "MySQL session disconnected,"
                                                   " reconnecting." );
                        versionRemove( "MySQL client" );
                        versionRemove( "MySQL server" );
                        connected = db_server_connect( protItem->sql );
                    }

                    if( !connected ) {
                        LogPrintNoArg( LOG_NOTICE, "MySQL reconnection failed, "
                                                   "retrying in 60s" );
                        sleep( 60 );
                    }
                }
                ProtectedDataUnlock( sql );
            }

            if( !BotDone ) {
                connected = TRUE;
                query = &item->queryTable[item->queryId];
                res = db_query( query->queryPattern, item->queryData, 
                                item->queryDataCount, &connected, &insertid );

                if( !connected ) {
                    LogPrintNoArg( LOG_NOTICE, "MySQL connection is gone, "
                                               "reconnecting" );
                }
            }
        } while( !BotDone && !connected );

        if( !BotDone ) {
            if( res || insertid ) {
                if( item->queryCallback ) {
                    item->queryCallback( res, item->queryData, 
                                         item->queryCallbackArg, insertid );
                } else if( query->queryChainFunc ) {
                    query->queryChainFunc( res, item );
                }
                mysql_free_result(res);
            }

            if( item->queryMutex ) {
                pthread_mutex_unlock( item->queryMutex );
            }

            if( !query->queryChainFunc ) {
                for( i = 0; i < item->queryDataCount; i++ ) {
                    if( !item->queryData[i].is_null || 
                        !(*item->queryData[i].is_null) ) {
                        free( item->queryData[i].buffer );
                    }
                }

                if( item->queryData ) {
                    free( item->queryData );
                }
            }

            free( item );
        }
    }

    LogPrintNoArg( LOG_NOTICE, "Ending MySQL thread" );
    mysql_thread_end();
    return(NULL);
}

void db_setup(void)
{
    MysqlData_t    *item;

    item = (MysqlData_t *)malloc(sizeof(MysqlData_t));
    if( !item ) {
        LogPrintNoArg( LOG_CRIT, "Unable to create a MySQL structure!!");
        exit(1);
    }

    sql = ProtectedDataCreate();
    if( !sql ) {
        LogPrintNoArg( LOG_CRIT, "Unable to create a MySQL protected "
                                 "structure!!");
        exit(1);
    }

    sql->data = (void *)item;

    item->buflen = MAX_STRING_LENGTH;

    if( !(item->sql = mysql_init(NULL)) ) {
        LogPrintNoArg( LOG_CRIT, "Unable to initialize a MySQL structure!!");
        exit(1);
    }

    if( !db_server_connect( item->sql ) ) {
        exit(1);
    }

    QueryQ = QueueCreate( 1024 );

    /* Start the thread */
    thread_create( &sqlThreadId, mysql_thread, NULL, "thread_mysql", NULL );
}

bool db_server_connect( MYSQL *mysql )
{
    my_bool         my_true;
    unsigned long   serverVers;
    static char     buf[30];

    LogPrint( LOG_CRIT, "Using database %s at %s:%d", mysql_db, mysql_host, 
              mysql_portnum);

    if( !mysql_real_connect(mysql, mysql_host, mysql_user, mysql_password, 
                            mysql_db, mysql_port, NULL, 0) ) {
        LogPrint(LOG_CRIT, "Unable to connect to the database - %s",
                           mysql_error(mysql) );
        return( FALSE );
    }

#ifdef MYSQL_OPT_RECONNECT
    /* Only defined in MySQL 5.0.13 and above, before that, it was always on */
    my_true = TRUE;
    mysql_options( mysql, MYSQL_OPT_RECONNECT, &my_true );
#else
    (void)my_true;
#endif

    snprintf( buf, 30, "%d.%d.%d", MYSQL_VERSION_ID / 10000,
                       (MYSQL_VERSION_ID / 100 ) % 100, 
                       MYSQL_VERSION_ID % 100 );
    LogPrint( LOG_CRIT, "MySQL client version %s", buf );
    versionAdd( "MySQL client", buf );

    serverVers = mysql_get_server_version( mysql );
    snprintf( buf, 30, "%ld.%ld.%ld", serverVers / 10000, 
                       (serverVers / 100) % 100,
                       serverVers % 100 );
    LogPrint( LOG_CRIT, "MySQL server version %s", buf );
    versionAdd( "MySQL server", buf );

    return( TRUE );
}

void db_thread_init( void )
{
    mysql_thread_init();
}

char *db_quote(char *string)
{
    int             len,
                    i,
                    j,
                    count;
    char           *retString;

    len = strlen(string);

    for(i = 0, count = 0; i < len; i++) {
        if( string[i] == '\'' || string[i] == '\"' || string[i] == '\\' ) {
            count++;
        }
    }

    if( !count ) {
        return( strdup(string) );
    }

    retString = (char *)malloc(len + count + 1);
    for(i = 0, j = 0; i < len; i++, j++) {
        if( string[i] == '\'' || string[i] == '\"' || string[i] == '\\' ) {
            retString[j++] = '\\';
        }
        retString[j] = string[i];
    }
    retString[j] = '\0';

    return( retString );
}


MYSQL_RES *db_query( const char *query, MYSQL_BIND *args, int arg_count, 
                     bool *connected, long *insertid )
{
    MYSQL_RES      *res;
    MysqlData_t    *item;
    char           *insert;
    int             buflen;
    int             len;
    int             count;
    static char     buf[128];
    char           *string;
    char           *sqlbuf;
    int             retval;

    ProtectedDataLock( sql );

    item = (MysqlData_t *)sql->data;
    sqlbuf = item->sqlbuf;
    sqlbuf[0] = '\0';
    buflen = item->buflen - 1;
    count = 0;
    *connected = TRUE;

    do {
        insert = strchr( query, '?' );
        if( !insert || !args ) {
            strncat( sqlbuf, query, buflen );
            continue;
        }

        if( !args ) {
            LogPrintNoArg( LOG_CRIT, "SQL malformed query!!" );
            ProtectedDataUnlock( sql );
            return( NULL );
        }

        count++;
        len = insert - query;
        if( buflen < len ) {
            /* Oh oh! */
            LogPrintNoArg( LOG_CRIT, "SQL buffer overflow!!" );
            ProtectedDataUnlock( sql );
            return( NULL );
        }
        strncat( sqlbuf, query, len );
        query = insert + 1;

        if( count > arg_count ) {
            insert = NULL;
            continue;
        }

        string = NULL;
        buf[0] = '\0';

        if( args->is_null && *args->is_null ) {
            len = 4;
            if( buflen < len ) {
                /* Oh oh! */
                LogPrintNoArg( LOG_CRIT, "SQL buffer overflow!!" );
                ProtectedDataUnlock( sql );
                return( NULL );
            }
            strncat( sqlbuf, "NULL", 4 );
            buflen -= len;
        } else {
            switch( args->buffer_type ) {
            case MYSQL_TYPE_TINY:
                snprintf( buf, 128, "%c", *(char *)(args->buffer) );
                string = db_quote(buf);
                len = strlen(string) + 2;
                break;
            case MYSQL_TYPE_SHORT:
                len = snprintf( buf, 128, "%d", *(short int *)(args->buffer) );
                break;
            case MYSQL_TYPE_LONG:
                len = snprintf( buf, 128, "%d", *(int *)(args->buffer) );
                break;
            case MYSQL_TYPE_LONGLONG:
                len = snprintf( buf, 128, "%lld", 
                                *(long long int *)(args->buffer) );
                break;
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_BLOB:
                if( args->buffer ) {
                    string = db_quote((char *)args->buffer);
                    len = strlen(string) + 2;
                } else {
                    len = 2;
                }
                break;
            default:
                continue;
                break;
            }

            if( buflen < len ) {
                /* Oh oh! */
                LogPrintNoArg( LOG_CRIT, "SQL buffer overflow!!" );
                ProtectedDataUnlock( sql );
                return( NULL );
            }

            if( string || buf[0] == '\0' ) {
                strcat( sqlbuf, "'" );
                if( string ) {
                    strncat( sqlbuf, string, len );
                    free( string );
                }
                strcat( sqlbuf, "'" );
            } else {
                strncat( sqlbuf, buf, len );
            }
        }
        args++;
    } while( insert );

    if( mysql_query(item->sql, sqlbuf) != 0 ) {
        retval = mysql_errno(item->sql);
        LogPrint( LOG_CRIT, "MySQL error %d: %s", retval,
                            mysql_error(item->sql) );
        LogPrint( LOG_CRIT, "MySQL query: %s", sqlbuf );

        if( retval == CR_SERVER_GONE_ERROR || retval == CR_SERVER_LOST ) {
            *connected = FALSE;
            ProtectedDataUnlock( sql );
            return( NULL );
        }
    }

    res = mysql_store_result(item->sql);

    if( insertid ) {
        *insertid = mysql_insert_id(item->sql);
    }

    ProtectedDataUnlock( sql );

    return( res );
}

/*
 * Query Queuing
 */
void db_queue_query( int queryId, QueryTable_t *queryTable,
                     MYSQL_BIND *queryData, int queryDataCount,
                     QueryResFunc_t queryCallback, void *queryCallbackArg,
                     pthread_mutex_t *queryMutex )
{
    static unsigned int sequence = 0;
    QueryItem_t        *item;

    item = (QueryItem_t *)malloc(sizeof(QueryItem_t));
    memset( item, 0, sizeof(QueryItem_t) );

    item->queryId = queryId;
    item->queryTable = queryTable;
    item->queryCallback = queryCallback;
    item->queryCallbackArg = queryCallbackArg;
    item->queryData = queryData;
    item->queryDataCount = queryDataCount;
    item->queryMutex = queryMutex;
    item->querySequence = (++sequence);

    if( queryMutex ) {
        pthread_mutex_lock( queryMutex );
    }

    QueueEnqueueItem( QueryQ, item );

    if( queryMutex ) {
        pthread_mutex_lock( queryMutex );
    }
}

/*
 * Bind values to MYSQL_BIND structures
 */

void bind_numeric( MYSQL_BIND *data, long long int value, 
                   enum enum_field_types type )
{
    void           *ptr;
    int             len;
    static my_bool  isnull = TRUE;

    if( !data ) {
        return;
    }

    data->buffer_type = type;

    switch( type ) {
    case MYSQL_TYPE_TINY:
        len = 1;
        break;
    case MYSQL_TYPE_SHORT:
        len = 2;
        break;
    case MYSQL_TYPE_LONG:
        len = 4;
        break;
    case MYSQL_TYPE_LONGLONG:
        len = 8;
        break;
    default:
        data->is_null = &isnull;
        return;
    }

    ptr = malloc(len);
    if( !ptr ) {
        data->is_null = &isnull;
        return;
    }
    data->buffer = ptr;

    switch( type ) {
    case MYSQL_TYPE_TINY:
        *(char *)ptr = (char)value;
        break;
    case MYSQL_TYPE_SHORT:
        *(short int *)ptr = (short int)value;
        break;
    case MYSQL_TYPE_LONG:
        *(int *)ptr = (int)value;
        break;
    case MYSQL_TYPE_LONGLONG:
        *(long long int *)ptr = (long long int)value;
        break;
    default:
        break;
    }
}

void bind_string( MYSQL_BIND *data, char *value, enum enum_field_types type )
{
    char           *ptr;
    static my_bool  isnull = TRUE;

    if( !data ) {
        return;
    }

    data->buffer_type = type;

    switch( type ) {
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_BLOB:
        break;
    default:
        data->is_null = &isnull;
        return;
    }

    if( !value ) {
        data->is_null = &isnull;
        return;
    }

    ptr = strdup( value );
    if( !ptr ) {
        data->is_null = &isnull;
        return;
    }
    data->buffer = ptr;
    data->buffer_length = strlen( value );
}

void bind_null_blob( MYSQL_BIND *data, void *value )
{
    static my_bool  isnull = TRUE;

    if( !data ) {
        return;
    }

    data->buffer = value;
    data->buffer_type = MYSQL_TYPE_BLOB;
    data->is_null = &isnull;
}

/*
 * User functions
 */

void db_load_servers(void)
{
    pthread_mutex_t    *mutex;

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, QueryTable, NULL, 0, result_load_servers, NULL, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );
}

/* Assumes that ServerTree is already locked */
void db_load_channels(void)
{
    pthread_mutex_t    *mutex;

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    serverTreeLoadChannels( ServerTree->root, mutex );

    pthread_mutex_destroy( mutex );
    free( mutex );
}

void serverTreeLoadChannels( BalancedBTreeItem_t *node, 
                             pthread_mutex_t *mutex )
{
    IRCServer_t    *server;
    MYSQL_BIND     *data;

    if( !node ) {
        return;
    }

    serverTreeLoadChannels( node->left, mutex );

    server = (IRCServer_t *)node->item;
    data = (MYSQL_BIND *)malloc(sizeof(MYSQL_BIND));
    memset( data, 0 , sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], server->serverId, MYSQL_TYPE_LONG );

    db_queue_query( 1, QueryTable, data, 1, result_load_channels, server, 
                    mutex );
    pthread_mutex_unlock( mutex );

    serverTreeLoadChannels( node->right, mutex );
}


void db_add_logentry( IRCChannel_t *channel, char *nick, IRCMsgType_t msgType, 
                      char *text, bool extract )
{
    char           *nickOnly;
    MYSQL_BIND     *data;
    struct timeval  tv;

    if( !channel || !nick || !text ) {
        return;
    }

    data = (MYSQL_BIND *)malloc(5 * sizeof(MYSQL_BIND));
    memset( data, 0, 5 * sizeof(MYSQL_BIND) );

    if( extract ) {
        nickOnly = (char *)malloc(strlen(nick));
        if( !nickOnly ) {
            nickOnly = nick;
        } else {
            BN_ExtractNick(nick, nickOnly, strlen(nick));
        }
    } else {
        nickOnly = nick;
    }

    gettimeofday( &tv, NULL );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], tv.tv_sec, MYSQL_TYPE_LONGLONG );
    bind_string( &data[2], nickOnly, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[3], msgType, MYSQL_TYPE_LONG );
    bind_string( &data[4], text, MYSQL_TYPE_BLOB );

    if( nickOnly != nick ) {
        free( nickOnly );
    }

    db_queue_query( 2, QueryTable, data, 5, NULL, NULL, NULL );
}


void db_update_nick( IRCChannel_t *channel, char *nick, bool present, 
                     bool extract )
{
    char           *nickOnly;
    MYSQL_BIND     *data;
    struct timeval  tv;

    if( !channel || !nick ) {
        return;
    }

    data = (MYSQL_BIND *)malloc(4 * sizeof(MYSQL_BIND));
    memset( data, 0, 4 * sizeof(MYSQL_BIND) );

    if( extract ) {
        nickOnly = (char *)malloc(strlen(nick));
        if( !nickOnly ) {
            nickOnly = nick;
        } else {
            BN_ExtractNick(nick, nickOnly, strlen(nick));
        }
    } else {
        nickOnly = nick;
    }

    gettimeofday( &tv, NULL );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], nickOnly, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[2], tv.tv_sec, MYSQL_TYPE_LONGLONG );
    bind_numeric( &data[3], present, MYSQL_TYPE_LONG );

    if( nickOnly != nick ) {
        free( nickOnly );
    }

    db_queue_query( 3, QueryTable, data, 4, NULL, NULL, NULL );
}

void db_flush_nicks( IRCChannel_t *channel )
{
    MYSQL_BIND     *data;

    data = (MYSQL_BIND *)malloc(4 * sizeof(MYSQL_BIND));
    memset( data, 0, 4 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );

    db_queue_query( 6, QueryTable, data, 1, NULL, NULL, NULL );
}

void db_flush_nick( IRCServer_t *server, char *nick, IRCMsgType_t type, 
                    char *text, char *newNick )
{
    char           *nickOnly;
    MYSQL_BIND     *data;

    nickOnly = (char *)malloc(strlen(nick));
    if( !nickOnly ) {
        nickOnly = nick;
    } else {
        BN_ExtractNick(nick, nickOnly, strlen(nick));
    }

    data = (MYSQL_BIND *)malloc(6 * sizeof(MYSQL_BIND));
    memset( data, 0, 6 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], server->serverId, MYSQL_TYPE_LONG );
    bind_string( &data[1], nickOnly, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[2], text, MYSQL_TYPE_BLOB );
    bind_string( &data[3], newNick, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[4], type, MYSQL_TYPE_LONG );
    bind_null_blob( &data[5], server );

    db_queue_query( 7, QueryTable, data, 6, NULL, NULL, NULL );

    if( nickOnly != nick ) {
        free( nickOnly );
    }
}

bool db_check_nick_notify( IRCChannel_t *channel, char *nick, int hours )
{
    bool                retval;
    pthread_mutex_t    *mutex;
    MYSQL_BIND         *data;
    struct timeval      tv;
    long long int       since;

    if( !channel || !nick ) {
        return( false );
    }

    data = (MYSQL_BIND *)malloc( 3 * sizeof(MYSQL_BIND));
    memset( data, 0, 3 * sizeof(MYSQL_BIND) );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    gettimeofday( &tv, NULL );
    since = tv.tv_sec - (3600 * hours);

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], nick, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[2], since, MYSQL_TYPE_LONGLONG );

    db_queue_query( 8, QueryTable, data, 3, result_check_nick_notify, &retval, 
                    mutex );

    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( retval );
}

void db_nick_history( IRCChannel_t *channel, char *nick, NickHistory_t type )
{
    MYSQL_BIND         *data;
    struct timeval      tv;

    if( !channel ) {
        return;
    }

    data = (MYSQL_BIND *)malloc( 4 * sizeof(MYSQL_BIND));
    memset( data, 0, 4 * sizeof(MYSQL_BIND) );

    gettimeofday( &tv, NULL );
    
    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], nick, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[2], type, MYSQL_TYPE_LONG );
    bind_numeric( &data[3], tv.tv_sec, MYSQL_TYPE_LONGLONG );

    db_queue_query( 9, QueryTable, data, 4, NULL, NULL, NULL );
}

void db_notify_nick( IRCChannel_t *channel, char *nick )
{
    MYSQL_BIND         *data;
    struct timeval      tv;

    if( !channel || !nick ) {
        return;
    }

    data = (MYSQL_BIND *)malloc( 3 * sizeof(MYSQL_BIND));
    memset( data, 0, 3 * sizeof(MYSQL_BIND) );

    gettimeofday( &tv, NULL );

    bind_numeric( &data[0], tv.tv_sec, MYSQL_TYPE_LONGLONG );
    bind_numeric( &data[1], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[2], nick, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 10, QueryTable, data, 3, NULL, NULL, NULL );
}


void db_get_plugins( BalancedBTree_t *tree )
{
    pthread_mutex_t    *mutex;

    if( !tree ) {
        return;
    }
    
    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 11, QueryTable, NULL, 0, result_get_plugins, tree, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );
}


char *db_get_seen( IRCChannel_t *channel, char *nick, bool privmsg )
{
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;
    char               *result;
    static int          privi;

    if( !channel || !nick ) {
        return( false );
    }

    privi = privmsg;

    data = (MYSQL_BIND *)malloc( 4 * sizeof(MYSQL_BIND));
    memset( data, 0, 4 * sizeof(MYSQL_BIND) );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    bind_numeric( &data[0], channel->channelId, MYSQL_TYPE_LONG );
    bind_string( &data[1], nick, MYSQL_TYPE_VAR_STRING );
    bind_null_blob( &data[2], channel );
    bind_null_blob( &data[3], &privi );

    db_queue_query( 12, QueryTable, data, 4, result_get_seen, &result, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( result );
}

char *db_get_setting( char *name )
{
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;
    char               *value;

    if( !name ) {
        return( NULL );
    }

    data = (MYSQL_BIND *)malloc( 1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    bind_string( &data[0], name, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 13, QueryTable, data, 1, result_get_setting, &value, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( value );
}


void db_set_setting( char *name, char *format, ... )
{
    MYSQL_BIND     *data;
    char            value[256];
    va_list         arguments;

    if( !name || !format ) {
        return;
    }

    data = (MYSQL_BIND *)malloc( 2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    va_start( arguments, format );
    vsnprintf( value, 256, format, arguments );
    va_end( arguments );

    bind_string( &data[0], name, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[1], value, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 14, QueryTable, data, 2, NULL, NULL, NULL);
}

AuthData_t *db_get_auth( char *nick )
{
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;
    AuthData_t         *authdata;

    if( !nick ) {
        return( NULL );
    }

    data = (MYSQL_BIND *)malloc( 1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    bind_string( &data[0], nick, MYSQL_TYPE_VAR_STRING );

    db_queue_query( 17, QueryTable, data, 1, result_get_auth, &authdata, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( authdata );
}

void db_free_auth( AuthData_t *auth )
{
    if( !auth ) {
        return;
    }

    free( auth->nick );
    free( auth->digest );
    free( auth->seed );
    free( auth->hash );
    free( auth );
}

void db_set_auth( char *nick, AuthData_t *auth )
{
    MYSQL_BIND     *data;
    my_bool         isnull;

    if( !nick || !auth ) {
        return;
    }
    
    isnull = TRUE;

    data = (MYSQL_BIND *)malloc( 2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_string( &data[0], nick, MYSQL_TYPE_VAR_STRING );
    bind_null_blob( &data[1], auth );

    db_queue_query( 18, QueryTable, data, 2, NULL, NULL, NULL);
}

void db_check_plugins( PluginDef_t *plugins, int count )
{
    PluginDef_t        *plugin;
    int                 i;
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;
    int                 found;

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    for( i = 0; i < count; i++ ) {
        plugin = &plugins[i];

        data = (MYSQL_BIND *)malloc( 1 * sizeof(MYSQL_BIND));
        memset( data, 0, 1 * sizeof(MYSQL_BIND) );

        bind_string( &data[0], plugin->pluginName, MYSQL_TYPE_VAR_STRING );

        db_queue_query( 21, QueryTable, data, 1, result_check_plugins, &found,
                        mutex );
        pthread_mutex_unlock( mutex );

        if( !found ) {
            LogPrint( LOG_NOTICE, "Adding new plugin to database: %s (%s)",
                                  plugin->pluginName, 
                                  (plugin->preload ? "enabled" : "disabled") );

            data = (MYSQL_BIND *)malloc( 4 * sizeof(MYSQL_BIND));
            memset( data, 0, 4 * sizeof(MYSQL_BIND) );

            bind_string( &data[0], plugin->pluginName, MYSQL_TYPE_VAR_STRING );
            bind_string( &data[1], plugin->libName, MYSQL_TYPE_VAR_STRING );
            bind_numeric( &data[2], plugin->preload, MYSQL_TYPE_LONG );
            bind_string( &data[3], plugin->arguments, MYSQL_TYPE_VAR_STRING );

            db_queue_query( 22, QueryTable, data, 4, NULL, NULL, mutex);
            pthread_mutex_unlock( mutex );
        }
    }

    pthread_mutex_destroy( mutex );
    free( mutex );
}

/*
 * Query chaining functions
 */

void chain_update_nick( MYSQL_RES *res, QueryItem_t *item )
{
    int             count;
    MYSQL_BIND     *data;
    MYSQL_BIND      temp[2];

    data = item->queryData;

    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }

    memcpy( temp, &data[2], 2 * sizeof(MYSQL_BIND) );
    memcpy( &data[2], &data[0], 2 * sizeof(MYSQL_BIND) );
    memcpy( &data[0], temp, 2 * sizeof(MYSQL_BIND) );

    if( count ) {
        /* update */
        db_queue_query( 4, QueryTable, data, 4, NULL, NULL, NULL );
    } else {
        /* insert */
        db_queue_query( 5, QueryTable, data, 4, NULL, NULL, NULL );
    }
}

void chain_flush_nick( MYSQL_RES *res, QueryItem_t *item )
{
    IRCChannel_t       *channel;
    char               *nick;
    IRCServer_t        *server;
    IRCMsgType_t        type; 
    char               *text;
    char               *newNick;
    int                 count;
    int                 i;
    MYSQL_ROW           row;
    MYSQL_BIND         *data;

    data = item->queryData;
    nick = (char *)data[1].buffer;
    text = (char *)data[2].buffer;

    if( data[3].is_null && *data[3].is_null ) {
        newNick = NULL;
    } else {
        newNick = data[3].buffer;
    }

    type = (IRCMsgType_t)*(int *)data[4].buffer;

    server = (IRCServer_t *)data[5].buffer;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        channel = FindChannelNum( server, atoi(row[0]) );
        db_update_nick( channel, nick, false, false );
        db_nick_history( channel, nick, HIST_LEAVE );
        if( newNick ) {
            db_update_nick( channel, newNick, true, false );
            db_nick_history( channel, newNick, HIST_JOIN );
        }
        db_add_logentry( channel, nick, type, text, false );
    }
}

void chain_set_setting( MYSQL_RES *res, QueryItem_t *item )
{
    int             count;
    MYSQL_BIND     *data;
    MYSQL_BIND      temp[1];

    data = item->queryData;
    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }

    if( count ) {
        /* update */
        /* Swap the order of the two parameters */
        memcpy( temp, data, sizeof( MYSQL_BIND ) );
        memcpy( data, &data[1], sizeof( MYSQL_BIND ) );
        memcpy( &data[1], temp, sizeof( MYSQL_BIND ) );
        db_queue_query( 15, QueryTable, data, 2, NULL, NULL, NULL );
    } else {
        /* insert */
        db_queue_query( 16, QueryTable, data, 2, NULL, NULL, NULL );
    }
}

void chain_set_auth( MYSQL_RES *res, QueryItem_t *item )
{
    int             count;
    char           *nick;
    AuthData_t     *auth;
    MYSQL_BIND     *data;
    
    data = item->queryData;
    nick = (char *)data[0].buffer;
    auth = (AuthData_t *)data[1].buffer;

    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }

    data = (MYSQL_BIND *)malloc(5 * sizeof(MYSQL_BIND));
    memset( data, 0, 5 * sizeof(MYSQL_BIND) );

    if( count ) {
        /* update */
        bind_string( &data[0], auth->digest, MYSQL_TYPE_VAR_STRING );
        bind_string( &data[1], auth->seed, MYSQL_TYPE_VAR_STRING );
        bind_string( &data[2], auth->hash, MYSQL_TYPE_VAR_STRING );
        bind_numeric( &data[3], auth->count, MYSQL_TYPE_LONG );
        bind_string( &data[4], nick, MYSQL_TYPE_VAR_STRING );

        db_queue_query( 19, QueryTable, data, 5, NULL, NULL, NULL );
    } else {
        /* insert */
        bind_string( &data[0], nick, MYSQL_TYPE_VAR_STRING );
        bind_string( &data[1], auth->digest, MYSQL_TYPE_VAR_STRING );
        bind_string( &data[2], auth->seed, MYSQL_TYPE_VAR_STRING );
        bind_string( &data[3], auth->hash, MYSQL_TYPE_VAR_STRING );
        bind_numeric( &data[4], auth->count, MYSQL_TYPE_LONG );

        db_queue_query( 20, QueryTable, data, 5, NULL, NULL, NULL );
    }
}

void cursesMySQLSchema( void *arg )
{
    db_queue_query( 23, QueryTable, NULL, 0, result_MySQL_Schema, NULL, NULL );
}

void db_update_channel( IRCChannel_t *channel )
{
    MYSQL_BIND             *data;
    pthread_mutex_t        *mutex;
    extern IRCChannel_t    *newChannel;

    data = (MYSQL_BIND *)malloc(7 * sizeof(MYSQL_BIND));
    memset( data, 0, 7 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], (channel->server ? channel->server->serverId : 0), 
                  MYSQL_TYPE_LONG );
    bind_string( &data[1], channel->channel, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[2], channel->url, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[3], channel->notifywindow, MYSQL_TYPE_LONG );
    bind_numeric( &data[4], channel->cmdChar, MYSQL_TYPE_TINY );
    bind_numeric( &data[5], channel->enabled, MYSQL_TYPE_LONG );
    bind_numeric( &data[6], channel->channelId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "Bot: channel %d: updating database", 
                          channel->channelId );
    if( channel == newChannel ) {
        mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init( mutex, NULL );

        db_queue_query( 26, QueryTable, data, 6, result_insert_channel, 
                        channel, mutex );

        pthread_mutex_unlock( mutex );
        pthread_mutex_destroy( mutex );
        free( mutex );

        if( channel->server ) {
            LinkedListAdd( channel->server->channels, 
                           (LinkedListItem_t *)channel, UNLOCKED, AT_TAIL );
            channel->justAdded = TRUE;
            newChannel = NULL;
        }

        cursesCancel( NULL, NULL );
    } else {
        db_queue_query( 24, QueryTable, data, 7, NULL, NULL, NULL );
    }
}

void db_update_server( IRCServer_t *server )
{
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;

    data = (MYSQL_BIND *)malloc(14 * sizeof(MYSQL_BIND));
    memset( data, 0, 14 * sizeof(MYSQL_BIND) );

    bind_string( &data[0], server->server, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[1], server->port, MYSQL_TYPE_LONG );
    bind_string( &data[2], server->password, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[3], server->nick, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[4], server->username, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[5], server->realname, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[6], server->nickserv, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[7], server->nickservmsg, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[8], server->floodInterval, MYSQL_TYPE_LONG );
    bind_numeric( &data[9], server->floodMaxTime, MYSQL_TYPE_LONG );
    bind_numeric( &data[10], server->floodBuffer, MYSQL_TYPE_LONG );
    bind_numeric( &data[11], server->floodMaxLine, MYSQL_TYPE_LONG );
    bind_numeric( &data[12], server->enabled, MYSQL_TYPE_LONG );
    bind_numeric( &data[13], server->serverId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "Bot: server %d: updating database", 
                          server->serverId );
    if( server->serverId == -1 ) {
        mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init( mutex, NULL );

        db_queue_query( 27, QueryTable, data, 13, result_insert_server, 
                        (void *)server, mutex );

        pthread_mutex_unlock( mutex );
        pthread_mutex_destroy( mutex );
        free( mutex );

        cursesCancel( NULL, NULL );
    } else {
        db_queue_query( 25, QueryTable, data, 14, NULL, NULL, NULL );
    }
}

/*
 * Query result callbacks
 */

/* Assumes that ServerTree is already locked */
void result_load_servers( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                          long insertid )
{
    IRCServer_t            *server;
    BalancedBTreeItem_t    *item;
    int                     count;
    int                     i;
    int                     len;
    MYSQL_ROW               row;
    int                     serverId;
    bool                    found;
    bool                    oldEnabled;
    char                   *threadName;
    bool                    killServer;
    int                     digits;
    int                     index;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    digits = (int)(log((double)count)/log(10.0) + 0.99999);

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        serverId = atoi(row[0]);

        item = BalancedBTreeFind( ServerTree, &serverId, LOCKED );
        if( item ) {
            server = (IRCServer_t *)item->item;
            found = TRUE;
            oldEnabled = server->enabled;
        } else {
            server = (IRCServer_t *)malloc(sizeof(IRCServer_t));
            if( !server ) {
                continue;
            }

            item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
            if( !item ) {
                free( server );
                continue;
            }
            memset( server, 0, sizeof(IRCServer_t) );
            found = FALSE;
            oldEnabled = FALSE;
        }

        server->serverId        = atoi(row[0]);

        if( found ) {
            free( server->server );
        }
        server->server          = strdup(row[1]);
        server->port            = (uint16)atoi(row[2]);

        if( found ) {
            free( server->password );
        }
        server->password        = strdup(row[3]);

        if( found ) {
            free( server->nick );
        }
        server->nick            = strdup(row[4]);

        if( found ) {
            free( server->username );
        }
        server->username        = strdup(row[5]);

        if( found ) {
            free( server->realname );
        }
        server->realname        = strdup(row[6]);

        if( found ) {
            free( server->nickserv );
        }
        server->nickserv        = strdup(row[7]);

        if( found ) {
            free( server->nickservmsg );
        }
        server->nickservmsg     = strdup(row[8]);
        server->floodInterval   = atoi(row[9]);
        server->floodMaxTime    = atoi(row[10]);
        server->floodBuffer     = atoi(row[11]);
        server->floodMaxLine    = atoi(row[12]);
        server->enabled         = ( atoi(row[13]) == 0 ? FALSE : TRUE );
        server->visited         = TRUE;
        server->newServer       = !found || (server->enabledChanged &&
                                             server->enabled);

        if( server->floodInterval <= 0 ) {
            server->floodInterval = 1;
        }

        if( server->floodMaxTime < 4 ) {
            server->floodMaxTime = 4;
        }

        killServer = FALSE;
        len = strlen(server->server) + strlen(server->nick) + 15;
        threadName = (char *)malloc(len) ;
        sprintf( threadName, "thread_%s@%s:%d", server->nick, server->server, 
                             server->port );

        if( found && server->threadName ) {
            if( strcmp( threadName, server->threadName ) ) {
                free( server->threadName );
                server->threadName = threadName;
                /* The server's been renamed, disconnect from the old one */
                killServer = TRUE;
                server->newServer = TRUE;
            } else {
                free( threadName );
            }
        } else {
            server->threadName = threadName;
        }

        len = strlen(server->server) + strlen(server->nick) + 18;
        threadName = (char *)malloc(len) ;
        sprintf( threadName, "tx_thread_%s@%s:%d", server->nick, server->server,
                             server->port );

        if( found && server->txThreadName ) {
            if( strcmp( threadName, server->txThreadName ) ) {
                free( server->txThreadName );
                server->txThreadName = threadName;
                killServer = TRUE;
                server->newServer = TRUE;
            } else {
                free( threadName );
            }
        } else {
            server->txThreadName = threadName;
        }

        len = strlen(server->server) + strlen(server->nick) + 18;
        threadName = (char *)malloc(len);
        sprintf( threadName, "%*d - %s@%s:%d", digits, server->serverId, 
                             server->nick, server->server, server->port );
        if( found && server->menuText ) {
            if( strcmp( threadName, server->menuText ) ) {
                cursesMenuItemRemove( 2, MENU_SERVERS, server->menuText );
                free( server->menuText );
                server->menuText = threadName;
                cursesMenuItemAdd( 2, MENU_SERVERS, server->menuText, 
                                   cursesServerDisplay, (void *)server );
            } else {
                free( threadName );
            }
        } else {
            server->menuText = threadName;
            cursesMenuItemAdd( 2, MENU_SERVERS, server->menuText, 
                               cursesServerDisplay, (void *)server );
        }

        if( server->justAdded ) {
            server->justAdded = FALSE;

            index = cursesMenuItemFind( 2, MENU_SERVERS, server->menuText );
            if( index != -1 ) {
                cursesMenuSetIndex( MENU_SERVERS, index );
            }
        }


        if( (found && (oldEnabled || server->enabledChanged) && 
             !server->enabled) || killServer ) {
            serverKill( item, server, killServer );
        }

        if( !server->floodList ) {
            server->floodList = LinkedListCreate();
        }

        server->oldEnabled = server->enabled;
        server->enabledChanged = FALSE;
        server->modified = FALSE;

        if( !found ) {
            item->item = (void *)server;
            item->key  = (void *)&server->serverId;
            BalancedBTreeAdd( ServerTree, item, LOCKED, FALSE );
        }
    }
    BalancedBTreeAdd( ServerTree, NULL, LOCKED, TRUE );
}

void result_load_channels( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                           long insertid )
{
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    int                 count;
    int                 i;
    MYSQL_ROW           row;
    LinkedListItem_t   *item;
    bool                found;
    int                 channelId;
    char               *fullSpec;
    char               *oldChannel;
    bool                oldEnabled;
    int                 digits;
    int                 index;

    server = (IRCServer_t *)arg;
    
    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    if( !server->channels ) {
        server->channels = LinkedListCreate();
        server->channelName = BalancedBTreeCreate( BTREE_KEY_STRING );
        server->channelNum  = BalancedBTreeCreate( BTREE_KEY_INT );
    }

    LinkedListLock( server->channels );
    BalancedBTreeLock( server->channelName );
    BalancedBTreeLock( server->channelNum );

    digits = (int)(log((double)count)/log(10.0) + 0.99999);

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        channelId = atoi(row[0]);
        oldChannel = NULL;
        oldEnabled = FALSE;

        for( found = FALSE, item = server->channels->head; item && !found;
             item = item->next ) {
            channel = (IRCChannel_t *)item;
            if( channel->channelId == channelId ) {
                found = TRUE;
                oldChannel = strdup( channel->channel );
                oldEnabled = channel->enabled;
                break;
            }
        }

        if( !found ) {
            channel = (IRCChannel_t *)malloc(sizeof(IRCChannel_t));
            if( !channel ) {
                continue;
            }
            memset( channel, 0, sizeof(IRCChannel_t) );
        }

        channel->channelId      = atoi(row[0]);

        if( found ) {
            free( channel->channel );
        }
        channel->channel        = strdup(row[1]);

        if( found ) {
            free( channel->url );
        }
        channel->url            = strdup(row[2]);
        channel->notifywindow   = atoi(row[3]);
        channel->cmdChar        = row[4][0];
        channel->enabled        = ( atoi(row[5]) == 0 ? FALSE : TRUE );
        channel->server         = server;
        if( !found ) {
            channel->joined     = FALSE;
        }
        channel->visited        = TRUE;
        channel->newChannel     = (!found || 
                                   ((!oldEnabled || channel->enabledChanged) &&
                                    channel->enabled));
        channel->modified       = FALSE;

        fullSpec = (char *)malloc(strlen(server->nick) + 10 + 
                                  strlen(server->server) +
                                  strlen(channel->channel));
        sprintf( fullSpec, "%s@%s:%d/%s", server->nick, server->server, 
                           server->port, channel->channel );
        if( found ) {
            if( strcmp( fullSpec, channel->fullspec ) || 
                ((oldEnabled || channel->enabledChanged) && 
                 !channel->enabled) ) {
                free( channel->fullspec );
                channel->fullspec = fullSpec;
                channelLeave( server, channel, oldChannel );
                channel->newChannel = TRUE;
                if( channel->enabled ) {
                    regexpBotCmdAdd( server, channel );
                }
            } else {
                free( fullSpec );
            }
        } else {
            channel->fullspec = fullSpec;
        }
        LogPrint( LOG_NOTICE, "%s%s", channel->fullspec,
                              ( channel->enabled ? "" : " (disabled)") );

        fullSpec = (char *)malloc(30 + strlen(channel->channel));
        sprintf( fullSpec, "%*d - (%d) %s", digits, channel->channelId, 
                 server->serverId, channel->channel );
        if( found && channel->menuText ) {
            if( strcmp( fullSpec, channel->menuText ) ) {
                cursesMenuItemRemove( 2, MENU_CHANNELS, channel->menuText );
                free( channel->menuText );
                channel->menuText = fullSpec;
                cursesMenuItemAdd( 2, MENU_CHANNELS, channel->menuText, 
                                   cursesChannelDisplay, channel );
            } else {
                free( fullSpec );
            }
        } else {
            channel->menuText = fullSpec;
            cursesMenuItemAdd( 2, MENU_CHANNELS, channel->menuText,
                               cursesChannelDisplay, channel );
        }

        if( channel->justAdded ) {
            index = cursesMenuItemFind( 2, MENU_CHANNELS, channel->menuText );
            if( index != -1 ) {
                cursesMenuSetIndex( MENU_CHANNELS, index );
            }
        }

        if( found && strcmp( oldChannel, channel->channel ) &&
            !channel->justAdded ) {
            channel->itemName.key = (void *)&oldChannel;
            BalancedBTreeRemove( server->channelName, &channel->itemName, 
                                 LOCKED, FALSE );
            channel->itemName.key = (void *)&channel->channel;
            BalancedBTreeAdd( server->channelName, &channel->itemName, LOCKED,
                              FALSE );
        }

        channel->enabledChanged = FALSE;
        channel->oldServer  = channel->server;
        channel->oldEnabled = channel->enabled; 
        
        if( !found || channel->justAdded ) {
            channel->itemName.item  = (void *)channel;
            channel->itemName.key   = (void *)&channel->channel;
            channel->itemNum.item   = (void *)channel;
            channel->itemNum.key    = (void *)&channel->channelId;

            BalancedBTreeAdd( server->channelName, &channel->itemName, LOCKED,
                              FALSE );
            BalancedBTreeAdd( server->channelNum, &channel->itemNum, LOCKED,
                              FALSE );

            if( !channel->justAdded ) {
                LinkedListAdd( server->channels, (LinkedListItem_t *)channel,
                               LOCKED, AT_TAIL );
            }

            if( server->enabled ) {
                regexpBotCmdAdd( server, channel );
            }
        }

        channel->justAdded = FALSE;

        if( oldChannel ) {
            free( oldChannel );
        }
    }

    /* Rebalance the trees */
    BalancedBTreeAdd( server->channelName, NULL, LOCKED, true );
    BalancedBTreeAdd( server->channelNum, NULL, LOCKED, true );

    LinkedListUnlock( server->channels );
    BalancedBTreeUnlock( server->channelName );
    BalancedBTreeUnlock( server->channelNum );
}

void result_insert_channel( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                            long insertid )
{
    IRCChannel_t       *channel;

    channel = (IRCChannel_t *)arg;
    LogPrint( LOG_DEBUG, "Inserted Channel: old ID %d, new ID %ld",
                         channel->channelId, insertid );
    if( channel->channelId == -1 ) {
        channel->channelId = insertid;
    }
}

void result_insert_server( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                           long insertid )
{
    IRCServer_t            *server;
    BalancedBTreeItem_t    *item;

    server = (IRCServer_t *)arg;
    LogPrint( LOG_DEBUG, "Inserted Server: old ID %d, new ID %ld", 
                         server->serverId, insertid );
    if( server->serverId == -1 ) {
        BalancedBTreeLock( ServerTree );
        item = BalancedBTreeFind( ServerTree, &server->serverId, LOCKED );
        if( item ) {
            BalancedBTreeRemove( ServerTree, item, LOCKED, FALSE );
            server->serverId = insertid;
            server->justAdded = TRUE;
            BalancedBTreeAdd( ServerTree, item, LOCKED, TRUE );
        }
        BalancedBTreeUnlock( ServerTree );
    }
}

void result_check_nick_notify( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                               long insertid )
{
    int             count;
    bool           *retval;

    retval = (bool *)arg;

    count = ( res ? mysql_num_rows(res) : 0 );

    if( count ) {
        *retval = TRUE;
    } else {
        *retval = FALSE;
    }
}

/*
 * Assumes that the tree is already locked!
 */
void result_get_plugins( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                         long insertid )
{
    Plugin_t               *plugin;
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    BalancedBTree_t        *tree;
    BalancedBTreeItem_t    *item;
    bool                    found;

    tree = (BalancedBTree_t *)arg;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        item = BalancedBTreeFind( tree, &row[0], LOCKED );
        if( !item ) {
            found = FALSE;
            plugin = (Plugin_t *)malloc(sizeof(Plugin_t));
            item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
            if( !plugin ) {
                continue;
            }

            if( !item ) {
                free( plugin );
                continue;
            }

            memset( plugin, 0, sizeof(Plugin_t) );
            plugin->newPlugin = TRUE;
        } else {
            found = TRUE;
            plugin = (Plugin_t *)item->item;
        }

        if( found ) {
            cursesMenuItemRemove( 2, MENU_PLUGINS, plugin->name );
            free( plugin->name );
        }
        plugin->name    = strdup(row[0]);
        cursesMenuItemAdd( 2, MENU_PLUGINS, plugin->name, cursesPluginDisplay, 
                           plugin );

        if( found ) {
            free( plugin->libName );
        }
        plugin->libName = strdup(row[1]);
        plugin->preload = atoi(row[2]);

        if( found ) {
            free( plugin->args );
        }
        plugin->args    = strdup(row[3]);
        plugin->visited = TRUE;
 
        if( !found ) {
            item->item = plugin;
            item->key  = (void *)&plugin->name;

            BalancedBTreeAdd( tree, item, LOCKED, FALSE );
        }
    }

    /* Rebalance the tree */
    BalancedBTreeAdd( tree, NULL, LOCKED, TRUE );
}


void result_get_seen( MYSQL_RES *res, MYSQL_BIND *input, void *arg, 
                      long insertid )
{
    char           *nick;
    IRCChannel_t   *channel;
    bool            privmsg;
    char           *result;
    char          **resultp;

    int             count;
    MYSQL_ROW       row;
    int             i;
    int             timeout;
    int             present;
    char            idle[256];
    char            idle2[256];
    int             day, hour, min, sec;
    int             len;

    nick = (char *)input[1].buffer;
    channel = (IRCChannel_t *)input[2].buffer;
    privmsg = (bool)*(int *)input[3].buffer;
    resultp = (char **)arg;

    if( !res || !(count = mysql_num_rows(res)) ) {
        len = strlen(nick) + 26 + (privmsg ? strlen(channel->channel) : 0 );
        result = (char *)malloc(len);
        if( privmsg ) {
            sprintf( result, "%s has not been seen in %s", nick, 
                     channel->channel );
        } else {
            sprintf( result, "%s has not been seen here", nick );
        }
        *resultp = result;
        return;
    }

    timeout = 0;
    present = 0;

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        timeout = atoi(row[0]);
        present = atoi(row[1]);
    }

    sec = timeout % 60;
    timeout /= 60;

    min = timeout % 60;
    timeout /= 60;

    hour = timeout % 24;
    timeout /= 24;

    day = timeout;

    idle[0] = '\0';

    if( day ) {
        sprintf( idle2, " %d day%s", day, (day == 1 ? "" : "s") );
        strcat( idle, idle2 );
    }

    if( hour ) {
        sprintf( idle2, " %d hour%s", hour, (hour == 1 ? "" : "s") );
        strcat( idle, idle2 );
    }

    if( min ) {
        sprintf( idle2, " %d minute%s", min, (min == 1 ? "" : "s") );
        strcat( idle, idle2 );
    }

    if( sec ) {
        sprintf( idle2, " %d second%s", sec, (sec == 1 ? "" : "s") );
        strcat( idle, idle2 );
    }

    if( !idle[0] ) {
        sprintf( idle, " 0 seconds" );
    }

    len = strlen(nick) + strlen(idle) + 
          (privmsg ? strlen(channel->channel) : 0);

    result = (char *)malloc(len + 64);

    if( present ) {
        if( privmsg ) {
            sprintf( result, "%s is in %s and has been idle for%s", nick, 
                     channel->channel, idle );
        } else {
            sprintf( result, "%s is here and has been idle for%s", nick, idle );
        }
    } else {
        if( privmsg ) {
            sprintf( result, "%s was last seen in %s%s ago", nick, 
                     channel->channel, idle );
        } else {
            sprintf( result, "%s was last seen%s ago", nick, idle );
        }
    }

    *resultp = result;
}


void result_get_setting( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                         long insertid )
{
    int             count;
    MYSQL_ROW       row;
    char           *value;
    char          **valuep;

    valuep = (char **)arg;

    if( !res || !(count = mysql_num_rows(res)) ) {
        *valuep = NULL;
        return;
    }

    row = mysql_fetch_row(res);
    value = strdup(row[0]);

    *valuep = value;
}


void result_get_auth( MYSQL_RES *res, MYSQL_BIND *input, void *arg, 
                      long insertid )
{
    int             count;
    MYSQL_ROW       row;
    AuthData_t     *data;
    AuthData_t    **datap;

    datap = (AuthData_t **)arg;

    if( !res || !(count = mysql_num_rows(res)) ) {
        *datap = NULL;
        return;
    }

    row = mysql_fetch_row(res);

    data = (AuthData_t *)malloc(sizeof(AuthData_t));
    if( !data ) {
        *datap = NULL;
        return;
    }

    data->server   = NULL;
    data->nick     = strdup(row[0]);
    data->digest   = strdup(row[1]);
    data->seed     = strdup(row[2]);
    data->hash     = strdup(row[3]);
    data->count    = atoi(row[4]);
    data->state    = AUTH_NONE;
    data->wakeTime = 0;

    *datap = data;
}

void result_check_plugins( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                           long insertid )
{
    int         *found;

    found = (int *)arg;

    if( !res || !(mysql_num_rows(res)) ) {
        *found = FALSE;
    } else {
        *found = TRUE;
    }
}

void result_MySQL_Schema( MYSQL_RES *res, MYSQL_BIND *input, void *arg, 
                          long insertid )
{
    int             i;
    int             count;
    MYSQL_ROW       row;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        cursesTextAdd( WINDOW_DETAILS, ALIGN_LEFT, 1, i, row[0] );
        cursesTextAdd( WINDOW_DETAILS, ALIGN_RIGHT, 1, i, row[1] );
    }
    cursesKeyhandleRegister( cursesDetailsKeyhandle );
}


/*
 * Helper functions to duplicate what's in newer versions of libmysqlclient
 */
#if ( MYSQL_VERSION_ID < 40100 )
unsigned long mysql_get_server_version(MYSQL *mysql)
{
    char           *orig;
    char           *verstring;
    char           *dot;
    unsigned long   version;

    verstring = strdup( mysql_get_server_info(mysql) );
    orig = verstring;

    dot = strchr( verstring, '.' );
    *dot = '\0';
    version = atol( verstring ) * 10000;
    verstring = dot + 1;

    dot = strchr( verstring, '.' );
    *dot = '\0';
    version += atol( verstring ) * 100;
    verstring = dot + 1;

    dot = strchr( verstring, '-' );
    if( dot ) {
        *dot = '\0';
    }
    version += atol( verstring );

    free( orig );

    return( version );
}
#endif

#if ( MYSQL_VERSION_ID < 40000 )
my_bool mysql_thread_init(void)
{
    return( 0 );
}
#endif


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
