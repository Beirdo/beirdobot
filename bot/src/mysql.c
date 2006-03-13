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
#include <stdlib.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"
#include "protected_data.h"
#include "logging.h"

static char ident[] _UNUSED_ =
    "$Id$";

typedef struct {
    MYSQL  *sql;
    char    sqlbuf[MAX_STRING_LENGTH];
    int     buflen;
} MysqlData_t;

static ProtectedData_t *sql;

/* Internal protos */
char *db_quote(char *string);
MYSQL_RES *db_query( char *format, ... );


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

    LogPrint( LOG_CRIT, "Using database %s at %s:%d", mysql_db, mysql_host, 
              mysql_portnum);

    if( !mysql_real_connect(item->sql, mysql_host, mysql_user, mysql_password, 
                            mysql_db, mysql_port, NULL, 0) ) {
        LogPrintNoArg(LOG_CRIT, "Unable to connect to the database");
        mysql_error(item->sql);
        exit(1);
    }
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
        if( string[i] == '\'' || string[i] == '\"' ) {
            count++;
        }
    }

    if( !count ) {
        return( strdup(string) );
    }

    retString = (char *)malloc(len + count + 1);
    for(i = 0, j = 0; i < len; i++, j++) {
        if( string[i] == '\'' || string[i] == '\"' ) {
            retString[j++] = '\\';
        }
        retString[j] = string[i];
    }
    retString[j] = '\0';

    return( retString );
}


MYSQL_RES *db_query( char *format, ... )
{
    MYSQL_RES      *res;
    MysqlData_t    *item;
    va_list         arguments;

    ProtectedDataLock( sql );

    item = (MysqlData_t *)sql->data;

    va_start( arguments, format );
    vsnprintf( item->sqlbuf, item->buflen, format, arguments );
    va_end( arguments );
    item->sqlbuf[item->buflen-1] = '\0';

    mysql_query(item->sql, item->sqlbuf);
    res = mysql_store_result(item->sql);

    ProtectedDataUnlock( sql );

    return( res );
}

void db_load_servers(void)
{
    IRCServer_t    *server;
    int             count;
    int             i;
    MYSQL_RES      *res;
    MYSQL_ROW       row;

    res = db_query( "SELECT `serverid`, `server`, `port`, `password`, `nick`, "
                    "`username`, `realname`, `nickserv`, `nickservmsg` "
                    "FROM `servers` ORDER BY `serverid`" );

    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        server = (IRCServer_t *)malloc(sizeof(IRCServer_t));
        if( !server ) {
            continue;
        }

        memset( server, 0, sizeof(IRCServer_t) );

        server->serverId        = atoi(row[0]);
        server->server          = strdup(row[1]);
        server->port            = (uint16)atoi(row[2]);
        server->password        = strdup(row[3]);
        server->nick            = strdup(row[4]);
        server->username        = strdup(row[5]);
        server->realname        = strdup(row[6]);
        server->nickserv        = strdup(row[7]);
        server->nickservmsg     = strdup(row[8]);

        LinkedListAdd( ServerList, (LinkedListItem_t *)server, UNLOCKED,
                       AT_TAIL );
    }

    mysql_free_result(res);
}

void db_load_channels(void)
{
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    int                 count;
    int                 i;
    MYSQL_RES          *res;
    MYSQL_ROW           row;

    LinkedListLock( ServerList );
    
    for( item = ServerList->head; item; item = item->next ) {
        server = (IRCServer_t *)item;

        res = db_query( "SELECT `chanid`, `channel`, `url`, `notifywindow`, "
                        "`cmdChar` "
                        "FROM `channels` WHERE `serverid` = %d "
                        "ORDER BY `chanid`", server->serverId );
        if( !res || !(count = mysql_num_rows(res)) ) {
            mysql_free_result(res);
            continue;
        }

        server->channels = LinkedListCreate();
        server->channelName = BalancedBTreeCreate( BTREE_KEY_STRING );
        server->channelNum  = BalancedBTreeCreate( BTREE_KEY_INT );
        LinkedListLock( server->channels );
        BalancedBTreeLock( server->channelName );
        BalancedBTreeLock( server->channelNum );

        for( i = 0; i < count; i++ ) {
            row = mysql_fetch_row(res);

            channel = (IRCChannel_t *)malloc(sizeof(IRCChannel_t));
            if( !channel ) {
                continue;
            }

            memset( channel, 0, sizeof(IRCChannel_t) );

            channel->channelId      = atoi(row[0]);
            channel->channel        = strdup(row[1]);
            channel->url            = strdup(row[2]);
            channel->notifywindow   = atoi(row[3]);
            channel->cmdChar        = row[4][0];
            channel->server         = server;
            channel->joined         = false;

            channel->fullspec       = (char *)malloc(strlen(server->nick) + 10 +
                                                     strlen(server->server) +
                                                     strlen(channel->channel));
            sprintf( channel->fullspec, "%s@%s:%d/%s", server->nick,
                     server->server, server->port, channel->channel );
            LogPrint( LOG_NOTICE, "%s", channel->fullspec );

            channel->itemName.item  = (void *)channel;
            channel->itemName.key   = (void *)&channel->channel;
            channel->itemNum.item   = (void *)channel;
            channel->itemNum.key    = (void *)&channel->channelId;

            BalancedBTreeAdd( server->channelName, &channel->itemName, LOCKED,
                              false );
            BalancedBTreeAdd( server->channelNum, &channel->itemNum, LOCKED,
                              false );
            LinkedListAdd( server->channels, (LinkedListItem_t *)channel,
                           LOCKED, AT_TAIL );
            regexpBotCmdAdd( server, channel );
        }
        mysql_free_result(res);

        /* Rebalance the trees */
        BalancedBTreeAdd( server->channelName, NULL, LOCKED, true );
        BalancedBTreeAdd( server->channelNum, NULL, LOCKED, true );

        LinkedListUnlock( server->channels );
        BalancedBTreeUnlock( server->channelName );
        BalancedBTreeUnlock( server->channelNum );
    }
    LinkedListUnlock( ServerList );
}


void db_add_logentry( IRCChannel_t *channel, char *nick, IRCMsgType_t msgType, 
                      char *text, bool extract )
{
    MYSQL_RES      *res;
    char           *nickOnly;
    char           *nickQuoted;
    char           *textQuoted;

    if( !channel || !nick || !text ) {
        return;
    }

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

    nickQuoted = db_quote(nickOnly);
    textQuoted = db_quote(text);
    if( nickOnly != nick ) {
        free( nickOnly );
    }

    if( nickQuoted && textQuoted ) {
        res = db_query( "INSERT INTO `irclog` (`chanid`, `timestamp`, "
                        "`nick`, `msgtype`, `message`) "
                        "VALUES ( %d, UNIX_TIMESTAMP(NOW()), '%s', %d, '%s' )",
                        channel->channelId, nickQuoted, msgType, textQuoted );
        mysql_free_result(res);
        free(nickQuoted);
        free(textQuoted);
    }
}


void db_update_nick( IRCChannel_t *channel, char *nick, bool present, 
                     bool extract )
{
    char           *nickOnly;
    char           *nickQuoted;
    int             count;
    MYSQL_RES      *res;

    if( !channel || !nick ) {
        return;
    }

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

    nickQuoted = db_quote(nickOnly);
    if( nickOnly != nick ) {
        free( nickOnly );
    }

    if( !nickQuoted ) {
        return;
    }

    res = db_query( "SELECT * FROM `nicks` WHERE `chanid` = %d AND "
                    "`nick` = '%s'", channel->channelId, nickQuoted );
    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }
    mysql_free_result(res);

    if( count ) {
        res = db_query( "UPDATE `nicks` "
                        "SET `lastseen` = UNIX_TIMESTAMP(NOW()), "
                        "`present` = %d WHERE `chanid` = %d AND "
                        "`nick` = '%s'",
                        present, channel->channelId, nickQuoted );
    } else {
        res = db_query( "INSERT INTO `nicks` (`chanid`, `nick`, `lastseen`, "
                        "`lastnotice`, `present`) "
                        "VALUES ( %d, '%s', UNIX_TIMESTAMP(NOW()), 0,  %d )",
                        channel->channelId, nickQuoted, present );
    }
    mysql_free_result(res);
    free(nickQuoted);
}

void db_flush_nicks( IRCChannel_t *channel )
{
    MYSQL_RES          *res;

    res = db_query( "UPDATE `nicks` SET `present` = 0 WHERE `chanid` = %d",
                    channel->channelId );
    mysql_free_result(res);
}

void db_flush_nick( IRCServer_t *server, char *nick, IRCMsgType_t type, 
                    char *text, char *newNick )
{
    IRCChannel_t       *channel;
    char               *nickOnly;
    char               *nickQuoted;
    int                 count;
    int                 i;
    MYSQL_RES          *res;
    MYSQL_ROW           row;

    nickOnly = (char *)malloc(strlen(nick));
    if( !nickOnly ) {
        nickOnly = nick;
    } else {
        BN_ExtractNick(nick, nickOnly, strlen(nick));
    }

    nickQuoted = db_quote(nickOnly);

    res = db_query( "SELECT DISTINCT `nicks`.`chanid` FROM `nicks`, `channels` "
                    "WHERE `nicks`.`chanid` = `channels`.`chanid` AND "
                    "`channels`.`serverid` = %d AND `nicks`.`nick` = '%s'",
                    server->serverId, nickQuoted );
    free( nickQuoted );

    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return;
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        channel = FindChannelNum( server, atoi(row[0]) );
        db_update_nick( channel, nickOnly, false, false );
        db_nick_history( channel, nickOnly, HIST_LEAVE );
        if( newNick ) {
            db_update_nick( channel, newNick, true, false );
            db_nick_history( channel, newNick, HIST_JOIN );
        }
        db_add_logentry( channel, nickOnly, type, text, false );
    }

    mysql_free_result(res);

    if( nickOnly != nick ) {
        free( nickOnly );
    }
}

bool db_check_nick_notify( IRCChannel_t *channel, char *nick, int hours )
{
    char           *nickQuoted;
    int             count;
    MYSQL_RES      *res;

    if( !channel || !nick ) {
        return( false );
    }

    nickQuoted = db_quote(nick);

    if( !nickQuoted ) {
        return( false );
    }

    res = db_query( "SELECT `lastnotice` FROM `nicks` WHERE "
                    "`chanid` = %d AND `nick` = '%s' AND "
                    "`lastnotice` <= UNIX_TIMESTAMP(NOW()) - (3600 * %d)",
                    channel->channelId, nickQuoted, hours );
    free(nickQuoted);

    count = ( res ? mysql_num_rows(res) : 0 );
    mysql_free_result(res);

    if( count ) {
        return( true );
    } else {
        return( false );
    }
}

void db_nick_history( IRCChannel_t *channel, char *nick, NickHistory_t type )
{
    MYSQL_RES      *res;
    char           *nickQuoted;
    static char    *empty = "";

    if( !channel ) {
        return;
    }

    if( nick ) {
        nickQuoted = db_quote(nick);
    } else {
        nickQuoted = empty;
    }

    res = db_query( "INSERT INTO `nickhistory` "
                    "( `chanid`, `nick`, `histType`, `timestamp` )"
                    "VALUES ( %d, '%s', %d, UNIX_TIMESTAMP(NOW()) )",
                    channel->channelId, nickQuoted, type );
    mysql_free_result(res);

    if( nickQuoted != empty ) {
        free(nickQuoted);
    }
}

void db_notify_nick( IRCChannel_t *channel, char *nick )
{
    MYSQL_RES      *res;
    char           *nickQuoted;

    nickQuoted = db_quote(nick);

    if( !nickQuoted ) {
        return;
    }

    res = db_query( "UPDATE `nicks` SET `lastnotice` = UNIX_TIMESTAMP(NOW()) "
                    "WHERE `chanid` = %d AND `nick` = '%s'",
                    channel->channelId, nickQuoted );
    mysql_free_result(res);
}


BalancedBTree_t *db_get_plugins( void )
{
    Plugin_t               *plugin;
    int                     count;
    int                     i;
    MYSQL_RES              *res;
    MYSQL_ROW               row;
    BalancedBTree_t        *tree;
    BalancedBTreeItem_t    *item;

    tree = BalancedBTreeCreate( BTREE_KEY_STRING );
    if( !tree ) {
        return( tree );
    }
    
    res = db_query( "SELECT `pluginName`, `libName`, `preload`, `arguments` "
                    "FROM `plugins`" );
    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return( tree );
    }

    BalancedBTreeLock( tree );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        plugin = (Plugin_t *)malloc(sizeof(Plugin_t));
        item   = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( !plugin ) {
            continue;
        }

        if( !item ) {
            free( plugin );
            continue;
        }

        memset( plugin, 0, sizeof(Plugin_t) );

        plugin->name    = strdup(row[0]);
        plugin->libName = strdup(row[1]);
        plugin->preload = atoi(row[2]);
        plugin->args    = strdup(row[3]);

        item->item = plugin;
        item->key  = (void *)&plugin->name;

        BalancedBTreeAdd( tree, item, LOCKED, FALSE );
    }
    mysql_free_result(res);

    /* Rebalance the tree */
    BalancedBTreeAdd( tree, NULL, LOCKED, TRUE );

    BalancedBTreeUnlock( tree );

    return( tree );
}

char *db_get_seen( IRCChannel_t *channel, char *nick, bool privmsg )
{
    char           *nickQuoted;
    int             count;
    MYSQL_RES      *res;
    MYSQL_ROW       row;
    int             i;
    int             timeout;
    int             present;
    char           *result;
    char            idle[256];
    char            idle2[256];
    int             day, hour, min, sec;
    int             len;

    if( !channel || !nick ) {
        return( false );
    }

    nickQuoted = db_quote(nick);

    if( !nickQuoted ) {
        return( false );
    }

    res = db_query( "SELECT UNIX_TIMESTAMP(NOW()) - `lastseen`, `present` "
                    "FROM `nicks` WHERE `chanid` = %d AND `nick` = '%s'",
                    channel->channelId, nickQuoted );
    free(nickQuoted);

    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        len = strlen(nick) + 26 + (privmsg ? strlen(channel->channel) : 0 );
        result = (char *)malloc(len);
        if( privmsg ) {
            sprintf( result, "%s has not been seen in %s", nick, 
                     channel->channel );
        } else {
            sprintf( result, "%s has not been seen here", nick );
        }
        return( result );
    }

    timeout = 0;
    present = 0;

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        timeout = atoi(row[0]);
        present = atoi(row[1]);
    }

    mysql_free_result(res);

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

    return( result );
}

char *db_get_setting( char *name )
{
    int             count;
    MYSQL_RES      *res;
    MYSQL_ROW       row;
    char           *value;

    if( !name ) {
        return( NULL );
    }

    res = db_query( "SELECT `value` "
                    "FROM `settings` WHERE `name` = '%s' LIMIT 1", name );
    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return( NULL );
    }

    row = mysql_fetch_row(res);
    value = strdup(row[0]);
    mysql_free_result(res);

    return( value );
}

void db_set_setting( char *name, char *format, ... )
{
    int             count;
    MYSQL_RES      *res;
    char            value[256];
    va_list         arguments;

    if( !name || !format ) {
        return;
    }

    va_start( arguments, format );
    vsnprintf( value, 256, format, arguments );
    va_end( arguments );

    res = db_query( "SELECT `value` FROM `settings` WHERE `name` = '%s' "
                    "LIMIT 1", name );
    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }
    mysql_free_result(res);

    if( count ) {
        res = db_query( "UPDATE `settings` SET `value` = '%s' "
                        "WHERE `name` = '%s'", value, name );
    } else {
        res = db_query( "INSERT INTO `settings` (`name`, `value`) "
                        "VALUES ( '%s', '%s' )", name, value );
    }
    mysql_free_result(res);
}

AuthData_t *db_get_auth( char *nick )
{
    int             count;
    MYSQL_RES      *res;
    MYSQL_ROW       row;
    AuthData_t     *data;
    char           *nickQuoted;

    if( !nick ) {
        return( NULL );
    }

    nickQuoted = db_quote(nick);

    if( !nickQuoted ) {
        return( NULL );
    }

    res = db_query( "SELECT `digest`, `seed`, `key`, `keyIndex` "
                    "FROM `userauth` WHERE `username` = '%s' LIMIT 1", 
                    nickQuoted );
    free( nickQuoted );

    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return( NULL );
    }

    row = mysql_fetch_row(res);

    data = (AuthData_t *)malloc(sizeof(AuthData_t));
    if( !data ) {
        mysql_free_result(res);
        return( NULL );
    }

    data->server   = NULL;
    data->nick     = strdup(nick);
    data->digest   = strdup(row[0]);
    data->seed     = strdup(row[1]);
    data->hash     = strdup(row[2]);
    data->count    = atoi(row[3]);
    data->state    = AUTH_NONE;
    data->wakeTime = 0;
    mysql_free_result(res);

    return( data );
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
    int             count;
    MYSQL_RES      *res;
    char           *nickQuoted;

    if( !nick || !auth ) {
        return;
    }

    nickQuoted = db_quote(nick);

    if( !nickQuoted ) {
        return;
    }

    res = db_query( "SELECT * FROM `userauth` WHERE `username` = '%s' "
                    "LIMIT 1", nickQuoted );
    if( !res || !(count = mysql_num_rows(res)) ) {
        count = 0;
    }
    mysql_free_result(res);

    if( count ) {
        res = db_query( "UPDATE `userauth` SET `digest` = '%s', `seed` = '%s', "
                        "`key` = '%s', `keyIndex` = %d "
                        "WHERE `username` = '%s'", auth->digest, auth->seed,
                        auth->hash, auth->count, nickQuoted );
    } else {
        res = db_query( "INSERT INTO `userauth` (`username`, `digest`, `seed`, "
                        "`key`, `keyIndex`) "
                        "VALUES ( '%s', '%s', '%s', '%s', %d )", nickQuoted,
                        auth->digest, auth->seed, auth->hash, auth->count );
    }
    mysql_free_result(res);
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
