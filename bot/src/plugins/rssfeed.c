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
#include "botnet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mysql.h>
#include <sys/time.h>
#include <time.h>
#include "structs.h"
#include "protos.h"
#include "queue.h"
#include "balanced_btree.h"
#include "linked_list.h"
#include "logging.h"
#include "mrss.h"


/* INTERNAL FUNCTION PROTOTYPES */
extern MYSQL_RES *db_query( char *format, ... );
void botCmdRssfeed( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg );
char *botHelpRssfeed( void );
void *rssfeed_thread(void *arg);
static int db_upgrade_schema( int current, int goal );
static void db_load_rssfeeds( void );
void db_update_lastpost( int feedId, int lastPost );


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#define DATEMSK_FILE "../lib/datemsk.txt"
#define CURRENT_SCHEMA_RSSFEED 1
#define MAX_SCHEMA_QUERY 100
typedef char *SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static char *defSchema[] = {
"CREATE TABLE `plugin_rssfeed` (\n"
"    `feedid` INT NOT NULL AUTO_INCREMENT ,\n"
"    `chanid` INT NOT NULL ,\n"
"    `url` VARCHAR( 255 ) NOT NULL ,\n"
"    `prefix` VARCHAR( 64 ) NOT NULL ,\n"
"    `timeout` INT NOT NULL ,\n"
"    `lastpost` INT NOT NULL ,\n"
"    PRIMARY KEY ( `feedid` )\n"
") TYPE = MYISAM ;\n"
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_RSSFEED] = {
    /* 0 -> 1 */
    { NULL }
};

typedef struct {
    int             feedId;
    int             serverId;
    int             chanId;
    char           *url;
    char           *prefix;
    time_t          timeout;
    time_t          lastPost;
    time_t          nextpoll;
    IRCServer_t    *server;
    IRCChannel_t   *channel;
} RssFeed_t;

typedef struct {
    RssFeed_t  *feed;
    time_t      pubTime;
    char       *title;
    char       *link;
} RssItem_t;


pthread_t           rssfeedThreadId;
BalancedBTree_t    *rssfeedTree;
BalancedBTree_t    *rssfeedActiveTree;
BalancedBTree_t    *rssItemTree;

void plugin_initialize( char *args )
{
    static char            *command = "rssfeed";
    char                   *verString;
    int                     ver;
    int                     printed;

    LogPrintNoArg( LOG_NOTICE, "Initializing rssfeed..." );

    setenv( "DATEMSK", DATEMSK_FILE, 1 );
    rssItemTree = BalancedBTreeCreate( BTREE_KEY_INT );

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting("dbSchemaRssfeed");
        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current RSSfeed database schema version %d", 
                                ver );
            LogPrint( LOG_CRIT, "Code supports version %d", 
                                CURRENT_SCHEMA_RSSFEED );
            printed = TRUE;
        }

        if( ver < CURRENT_SCHEMA_RSSFEED ) {
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA_RSSFEED );
        }
    } while( ver < CURRENT_SCHEMA_RSSFEED );

    db_load_rssfeeds();

    thread_create( &rssfeedThreadId, rssfeed_thread, NULL, "thread_rssfeed" );
    botCmd_add( (const char **)&command, botCmdRssfeed, botHelpRssfeed );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing rssfeed..." );
    botCmd_remove( "rssfeed" );
}

void *rssfeed_thread(void *arg)
{
    mrss_t                 *data;
    mrss_item_t            *rssItem;
    mrss_error_t            ret;
    time_t                  nextpoll;
    struct timeval          now;
    time_t                  delta;
    BalancedBTreeItem_t    *item;
    RssFeed_t              *feed;
    struct tm               tm;
    RssItem_t              *itemData;
    time_t                  pubTime;
    time_t                  lastPost;
    static char             buf[255];
    static char             message[1024];
    LinkedListItem_t       *listItem;
    bool                    found;
    IRCServer_t            *server;
    int                     count;
    int                     retval;

    db_thread_init();

    LogPrintNoArg( LOG_NOTICE, "Starting RSSfeed thread..." );

    sleep(5);

    while( !GlobalAbort ) {
        BalancedBTreeLock( rssfeedActiveTree );
        item = BalancedBTreeFindLeast( rssfeedActiveTree->root );
        BalancedBTreeUnlock( rssfeedActiveTree );

        gettimeofday( &now, NULL );

        delta = 60;
        if( !item ) {
            /* Nothing configured to be active, check in 15min */
            delta = 900;
            goto DelayPoll;
        } 
        
        feed = (RssFeed_t *)item->item;
        nextpoll = feed->nextpoll;
        if( nextpoll > now.tv_sec + 15 || !ServerList ) {
            delta = nextpoll - now.tv_sec;
            goto DelayPoll;
        }

        /* Trigger all feeds expired or to expire in <= 15s */
        BalancedBTreeLock( rssfeedActiveTree );
        BalancedBTreeLock( rssItemTree );
        for( found = FALSE; item && !found ; 
             item = BalancedBTreeFindLeast( rssfeedActiveTree->root ) ) {
            gettimeofday( &now, NULL );
            feed = (RssFeed_t *)item->item;
            if( feed->nextpoll > now.tv_sec + 15 ) {
                delta = feed->nextpoll - now.tv_sec;
                found = TRUE;
                continue;
            }

            if( (!feed->server || !feed->channel) && ServerList ) {
                LinkedListLock( ServerList );
                for( listItem = ServerList->head, found = FALSE; 
                     listItem && !found; listItem = listItem->next ) {
                    server = (IRCServer_t *)listItem;
                    if( server->serverId == feed->serverId ) {
                        found = TRUE;
                        feed->server = server;
                    }
                }
                LinkedListUnlock( ServerList );

                feed->channel = FindChannelNum( feed->server, feed->chanId );
            }

            /* This feed needs to be polled now!   Remove it, and requeue it
             * in the tree, then poll it
             */
            LogPrint( LOG_NOTICE, "RSS: polling feed %d in %s", feed->feedId,
                                  feed->channel->fullspec );

            if( !feed->channel->joined ) {
                LogPrint( LOG_NOTICE, "RSS: feed %d: delaying 60s, not "
                                      "joined yet", feed->feedId );
                feed->nextpoll = now.tv_sec + 60;
                continue;
            }

            feed->nextpoll = now.tv_sec + feed->timeout;
            BalancedBTreeRemove( rssfeedActiveTree, item, LOCKED, FALSE );
            BalancedBTreeAdd( rssfeedActiveTree, item, LOCKED, FALSE );

            lastPost = feed->lastPost;

            ret = mrss_parse_url( feed->url, &data );
            if( ret ) {
                LogPrint( LOG_NOTICE, "RSS feed %d: error %s", feed->feedId,
                                      mrss_strerror(ret) );
                continue;
            }

            LogPrint( LOG_NOTICE, "RSS: feed %d: parsing", feed->feedId,
                                  feed->channel->fullspec );
            count = 0;
            rssItem = data->item;
            while( rssItem ) {
                retval = getdate_r( rssItem->pubDate, &tm );
                pubTime = mktime( &tm );

                if( retval ) {
                    LogPrint( LOG_DEBUG, "ret: %d  pub: %s  pubTime: %ld", 
                                         retval, rssItem->pubDate, pubTime );
                }

                if( retval == 0 && pubTime > feed->lastPost ) {
                    itemData = (RssItem_t *)malloc(sizeof(RssItem_t));
                    itemData->feed    = feed;
                    itemData->pubTime = pubTime;
                    itemData->title   = (rssItem->title ? 
                                         strdup(rssItem->title) : NULL);
                    itemData->link    = (rssItem->link ? 
                                         strdup(rssItem->link) : NULL);

                    for( item = BalancedBTreeFind( rssItemTree, 
                                                   &itemData->pubTime,
                                                   LOCKED) ; item ;
                         item = BalancedBTreeFind( rssItemTree, 
                                                   &itemData->pubTime,
                                                   LOCKED) ) {
                        itemData->pubTime++;
                    }

                    item = (BalancedBTreeItem_t *)
                               malloc(sizeof(BalancedBTreeItem_t));
                    item->item = (void *)itemData;
                    item->key  = (void *)&itemData->pubTime;
                    BalancedBTreeAdd( rssItemTree, item, LOCKED, FALSE );

                    if( pubTime > lastPost ) {
                        lastPost = pubTime;
                    }
                    count++;
                }
                rssItem = rssItem->next;
            }

            LogPrint( LOG_NOTICE, "RSS: feed %d: %d new post%s", feed->feedId,
                                  count, (count == 1 ? "" : "s") );

            if( lastPost > feed->lastPost ) {
                db_update_lastpost( feed->feedId, lastPost );
                feed->lastPost = lastPost;
            }

            mrss_free( data );
        }

        /* Rebalance the trees */
        BalancedBTreeAdd( rssfeedActiveTree, NULL, LOCKED, TRUE );
        BalancedBTreeAdd( rssItemTree, NULL, LOCKED, TRUE );
        BalancedBTreeUnlock( rssfeedActiveTree );

        for( item = BalancedBTreeFindLeast( rssItemTree->root ) ;
             item ; item = BalancedBTreeFindLeast( rssItemTree->root ) ) {

            itemData = (RssItem_t *)item->item;
            feed    = itemData->feed;
            pubTime = itemData->pubTime;

            gmtime_r( &pubTime, &tm );
            strftime( buf, sizeof(buf), "%d %b %Y %H:%M %z (%Z)", &tm );
            sprintf( message, "RSS: [%s] at %s - %s", feed->prefix, buf,
                              itemData->title );
            if( itemData->link ) {
                sprintf( buf, " (%s)", itemData->link );
                strcat( message, buf );
            }

            LoggedChannelMessage( feed->server, feed->channel, message );
            LogPrint( LOG_NOTICE, "RSS: feed %d: %s", feed->feedId, message );

            if( itemData->link ) {
                free( itemData->link );
            }

            if( itemData->title ) {
                free( itemData->title );
            }


            BalancedBTreeRemove( rssItemTree, item, LOCKED, FALSE );
            free( itemData );
            free( item );
        }
        BalancedBTreeUnlock( rssItemTree );

    DelayPoll:
        LogPrint( LOG_NOTICE, "RSS: sleeping for %ds", delta );
        sleep( delta );
    }

    return( NULL );
}

void botCmdRssfeed( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg )
{
}

char *botHelpRssfeed( void )
{
    static char *help = "Starts and stops rssfeed notification for a channel, "
                        "per feed.  Must be authenticated for use.  "
                        "Syntax: (in channel) rssfeed start feed / "
                        "rssfeed stop feed  "
                        "(in privmsg) rssfeed start feed channel / "
                        "rssfeed stop feed channel";
    
    return( help );
}

static int db_upgrade_schema( int current, int goal )
{
    int                 i;
    MYSQL_RES          *res;

    if( current >= goal ) {
        return( current );
    }

    if( current == 0 ) {
        /* There is no dbSchema, assume that it is an empty database, populate
         * with the default schema
         */
        LogPrint( LOG_ERR, "Initializing RSSfeed database to schema version %d",
                  CURRENT_SCHEMA_RSSFEED );
        for( i = 0; i < defSchemaCount; i++ ) {
            res = db_query( defSchema[i] );
            mysql_free_result(res);
        }
        db_set_setting("dbSchemaRssfeed", "%d", CURRENT_SCHEMA_RSSFEED);
        return( CURRENT_SCHEMA_RSSFEED );
    }

    LogPrint( LOG_ERR, "Upgrading RSSfeed database from schema version %d to "
                       "%d", current, current+1 );
    for( i = 0; schemaUpgrade[current][i]; i++ ) {
        res = db_query( schemaUpgrade[current][i] );
        mysql_free_result(res);
    }

    current++;

    db_set_setting("dbSchemaRssfeed", "%d", current);
    return( current );
}

static void db_load_rssfeeds( void )
{
    int                     count;
    int                     i;
    MYSQL_RES              *res;
    MYSQL_ROW               row;
    RssFeed_t              *data;
    BalancedBTreeItem_t    *item;
    struct timeval          tv;
    time_t                  nextpoll;

    rssfeedTree = BalancedBTreeCreate( BTREE_KEY_INT );
    rssfeedActiveTree = BalancedBTreeCreate( BTREE_KEY_INT );

    res = db_query( "SELECT a.`feedid`, a.`chanid`, b.`serverid`, a.`url`, "
                    "a.`prefix`, a.`timeout`, a.`lastpost` "
                    "FROM `plugin_rssfeed` AS a, `channels` AS b "
                    "WHERE a.`chanid` = b.`chanid` "
                    "ORDER BY a.`feedid` ASC" );
    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return;
    }

    gettimeofday( &tv, NULL );
    nextpoll = tv.tv_sec;

    BalancedBTreeLock( rssfeedTree );
    BalancedBTreeLock( rssfeedActiveTree );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        data = (RssFeed_t *)malloc(sizeof(RssFeed_t));
        memset( data, 0x00, sizeof(RssFeed_t) );

        data->feedId   = atoi(row[0]);
        data->chanId   = atoi(row[1]);
        data->serverId = atoi(row[2]);
        data->url      = strdup(row[3]);
        data->prefix   = strdup(row[4]);
        data->timeout  = atoi(row[5]);
        data->lastPost = atoi(row[6]);
        data->nextpoll = nextpoll++;

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)data;
        item->key  = (void *)&data->nextpoll;
        BalancedBTreeAdd( rssfeedTree, item, LOCKED, FALSE );

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)data;
        item->key  = (void *)&data->nextpoll;
        BalancedBTreeAdd( rssfeedActiveTree, item, LOCKED, FALSE );
        LogPrint( LOG_NOTICE, "RSS: Loaded %d (%s): server %d, channel %d, "
                              "timeout %d", data->feedId, data->prefix, 
                              data->serverId, data->chanId, data->timeout );
    }
    mysql_free_result(res);

    BalancedBTreeAdd( rssfeedTree, NULL, LOCKED, TRUE );
    BalancedBTreeAdd( rssfeedActiveTree, NULL, LOCKED, TRUE );

    BalancedBTreeUnlock( rssfeedTree );
    BalancedBTreeUnlock( rssfeedActiveTree );
}

void db_update_lastpost( int feedId, int lastPost ) {
    MYSQL_RES              *res;

    LogPrint( LOG_NOTICE, "RSS: feed %d: updating lastpost to %d", feedId,
                          lastPost );
    res = db_query( "UPDATE `plugin_rssfeed` SET `lastpost` = %d "
                    "WHERE `feedid` = %d", lastPost, feedId );
    mysql_free_result(res);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
