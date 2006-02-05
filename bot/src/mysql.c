/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  havokmud is free software; you can redistribute it and/or modify
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

#include <stdio.h>
#include <string.h>
#include <mysql.h>
#include <stdlib.h>
#include "protos.h"
#include "botnet.h"
#include "environment.h"
#include "structs.h"

static char ident[] _UNUSED_ =
    "$Id$";

static MYSQL *sql;

static char sqlbuf[MAX_STRING_LENGTH] _UNUSED_;

/* Internal protos */
char *db_quote(char *string);


void db_setup(void)
{
    if( !(sql = mysql_init(NULL)) ) {
        fprintf(stderr, "Unable to initialize a MySQL structure!!\n");
        exit(1);
    }

    printf("Using database %s at %s:%d\n", mysql_db, mysql_host, mysql_portnum);

    if( !mysql_real_connect(sql, mysql_host, mysql_user, mysql_password, 
                            mysql_db, mysql_port, NULL, 0) ) {
        fprintf(stderr, "Unable to connect to the database\n");
        mysql_error(sql);
        exit(1);
    }
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


void db_load_servers(void)
{
    IRCServer_t    *server;
    int             count;
    int             i;
    MYSQL_RES      *res;
    MYSQL_ROW       row;

    strcpy(sqlbuf, "SELECT `serverid`, `server`, `port`, `nick`, `username`, "
                   "`realname`, `nickserv`, `nickservmsg` FROM `servers` "
                   "ORDER BY `serverid`" );
    mysql_query(sql, sqlbuf);

    res = mysql_store_result(sql);
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
        server->nick            = strdup(row[3]);
        server->username        = strdup(row[4]);
        server->realname        = strdup(row[5]);
        server->nickserv        = strdup(row[6]);
        server->nickservmsg     = strdup(row[7]);

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

        sprintf(sqlbuf, "SELECT `chanid`, `channel` FROM `channels` "
                        "WHERE `serverid` = %d ORDER BY `chanid`",
                        server->serverId );
        mysql_query(sql, sqlbuf);

        res = mysql_store_result(sql);
        if( !res || !(count = mysql_num_rows(res)) ) {
            mysql_free_result(res);
            continue;
        }

        server->channels = LinkedListCreate();

        for( i = 0; i < count; i++ ) {
            row = mysql_fetch_row(res);

            channel = (IRCChannel_t *)malloc(sizeof(IRCChannel_t));
            if( !channel ) {
                continue;
            }

            memset( channel, 0, sizeof(IRCChannel_t) );

            channel->channelId      = atoi(row[0]);
            channel->channel        = strdup(row[1]);
            channel->server         = server;
            channel->joined         = false;

            LinkedListAdd( server->channels, (LinkedListItem_t *)channel, 
                           UNLOCKED, AT_TAIL );
        }

        mysql_free_result(res);
    }
    LinkedListUnlock( ServerList );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
