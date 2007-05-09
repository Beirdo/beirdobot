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
#define _BSD_SOURCE
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



/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#ifndef DATEMSK_PATH
#define DATEMSK_PATH "../lib"
#endif
#define DATEMSK_FILE DATEMSK_PATH "/datemsk.txt"
#define CURRENT_SCHEMA_RSSFEED 2
#define MAX_SCHEMA_QUERY 100
typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_rssfeed` (\n"
    "    `feedid` INT NOT NULL AUTO_INCREMENT ,\n"
    "    `chanid` INT NOT NULL ,\n"
    "    `url` VARCHAR( 255 ) NOT NULL ,\n"
    "    `prefix` VARCHAR( 64 ) NOT NULL ,\n"
    "    `timeout` INT NOT NULL ,\n"
    "    `lastpost` INT NOT NULL ,\n"
    "    `feedoffset` INT NOT NULL ,\n"
    "    PRIMARY KEY ( `feedid` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_RSSFEED] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } },
    /* 1 -> 2 */
    { { "ALTER TABLE `plugin_rssfeed` ADD `feedoffset` INT NOT NULL ;", NULL,
        NULL, FALSE },
      { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t rssfeedQueryTable[] = {
    /* 0 */
    { "SELECT a.`feedid`, a.`chanid`, b.`serverid`, a.`url`, a.`prefix`, "
      "a.`timeout`, a.`lastpost`, a.`feedoffset` FROM `plugin_rssfeed` AS a, "
      "`channels` AS b WHERE a.`chanid` = b.`chanid` ORDER BY a.`feedid` ASC",
      NULL, NULL, FALSE },
    /* 1 */
    { "UPDATE `plugin_rssfeed` SET `lastpost` = ? WHERE `feedid` = ?", NULL,
      NULL, FALSE }
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
    bool            enabled;
    long            offset;
} RssFeed_t;

typedef struct {
    RssFeed_t  *feed;
    time_t      pubTime;
    char       *title;
    char       *link;
} RssItem_t;

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdRssfeed( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag );
char *botHelpRssfeed( void *tag );
void *rssfeed_thread(void *arg);
static int db_upgrade_schema( int current, int goal );
static void db_load_rssfeeds( void );
static void result_load_rssfeeds( MYSQL_RES *res, MYSQL_BIND *input, 
                                  void *args );
void db_update_lastpost( int feedId, int lastPost );
void rssfeedFindUnconflictingTime( BalancedBTree_t *tree, time_t *key );
char *botRssfeedDepthFirst( BalancedBTreeItem_t *item, IRCServer_t *server,
                            IRCChannel_t *channel, bool filter );
char *botRssfeedDump( BalancedBTreeItem_t *item );
char *rssfeedShowDetails( RssFeed_t *feed );


/* INTERNAL VARIABLES  */
pthread_t               rssfeedThreadId;
BalancedBTree_t        *rssfeedTree;
BalancedBTree_t        *rssfeedActiveTree;
BalancedBTree_t        *rssItemTree;
static bool             threadAbort = FALSE;
static pthread_mutex_t  shutdownMutex;
static pthread_mutex_t  signalMutex;
static pthread_cond_t   kickCond;


void plugin_initialize( char *args )
{
    static char            *command = "rssfeed";
    char                   *verString;
    int                     ver;
    int                     printed;

    LogPrintNoArg( LOG_NOTICE, "Initializing rssfeed..." );
    LogPrint( LOG_NOTICE, "Date Mask File: %s", DATEMSK_FILE );

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

    pthread_mutex_init( &shutdownMutex, NULL );
    pthread_mutex_init( &signalMutex, NULL );
    pthread_cond_init( &kickCond, NULL );

    thread_create( &rssfeedThreadId, rssfeed_thread, NULL, "thread_rssfeed" );
    botCmd_add( (const char **)&command, botCmdRssfeed, botHelpRssfeed, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing rssfeed..." );
    botCmd_remove( "rssfeed" );

    threadAbort = TRUE;

    /* Kick the thread to tell it it can quit now */
    pthread_mutex_lock( &signalMutex );
    pthread_cond_broadcast( &kickCond );
    pthread_mutex_unlock( &signalMutex );

    /* Clean up stuff once the thread stops */
    pthread_mutex_lock( &shutdownMutex );
    pthread_mutex_destroy( &shutdownMutex );

    pthread_mutex_lock( &signalMutex );
    pthread_cond_broadcast( &kickCond );
    pthread_cond_destroy( &kickCond );
    pthread_mutex_destroy( &signalMutex );

    BalancedBTreeLock( rssfeedTree );
    BalancedBTreeDestroy( rssfeedTree );

    BalancedBTreeLock( rssfeedActiveTree);
    BalancedBTreeDestroy( rssfeedActiveTree );
    
    BalancedBTreeLock( rssItemTree );
    BalancedBTreeDestroy( rssItemTree );

    thread_deregister( rssfeedThreadId );
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
    bool                    done;
    long                    localoffset;
    struct timespec         ts;
    char                   *cr;

    pthread_mutex_lock( &shutdownMutex );
    db_thread_init();

    LogPrintNoArg( LOG_NOTICE, "Starting RSSfeed thread" );

    sleep(5);

    while( !GlobalAbort && !threadAbort ) {
        BalancedBTreeLock( rssfeedActiveTree );
        item = BalancedBTreeFindLeast( rssfeedActiveTree->root );
        BalancedBTreeUnlock( rssfeedActiveTree );

        gettimeofday( &now, NULL );
        localtime_r( &now.tv_sec, &tm );
        localoffset = tm.tm_gmtoff;

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
        for( done = FALSE; item && !done && !threadAbort ; 
             item = BalancedBTreeFindLeast( rssfeedActiveTree->root ) ) {
            gettimeofday( &now, NULL );
            feed = (RssFeed_t *)item->item;
            LogPrint( LOG_NOTICE, "RSS: feed %d poll in %lds", feed->feedId,
                                  feed->nextpoll - now.tv_sec );
            if( feed->nextpoll > now.tv_sec + 15 ) {
                delta = feed->nextpoll - now.tv_sec;
                done = TRUE;
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
            if( feed->channel && feed->channel->fullspec ) {
                LogPrint( LOG_NOTICE, "RSS: polling feed %d in %s", 
                                      feed->feedId, feed->channel->fullspec );
            }

            if( !feed->channel || !feed->channel->joined ) {
                LogPrint( LOG_NOTICE, "RSS: feed %d: delaying 60s, not "
                                      "joined yet", feed->feedId );
                feed->nextpoll = now.tv_sec + 60;
                continue;
            }

            BalancedBTreeRemove( rssfeedActiveTree, item, LOCKED, FALSE );

            /* Adjust the poll time to avoid conflict */
            feed->nextpoll = now.tv_sec + feed->timeout;
            rssfeedFindUnconflictingTime( rssfeedActiveTree, &feed->nextpoll );
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

                /* Adjust for the time offset in the feed's timestamp as 
                 * getdate assumes the date is in local timezone
                 */
                pubTime += localoffset - feed->offset;

                if( retval ) {
                    LogPrint( LOG_DEBUG, "ret: %d  pub: %s  pubTime: %ld", 
                                         retval, rssItem->pubDate, pubTime );
                }

                if( retval == 0 && pubTime > feed->lastPost ) {
                    itemData = (RssItem_t *)malloc(sizeof(RssItem_t));
                    itemData->feed    = feed;
                    itemData->pubTime = pubTime;
                    if( rssItem->title ) {
                        while( (cr = strchr(rssItem->title, '\n') ) ||
                               (cr = strchr(rssItem->title, '\r') ) ) {
                            *cr = ' ';
                        }
                    }
                    itemData->title   = (rssItem->title ? 
                                         strdup(rssItem->title) : NULL);
                    itemData->link    = (rssItem->link ? 
                                         strdup(rssItem->link) : NULL);

                    /* Adjust the poll time before adding */
                    rssfeedFindUnconflictingTime( rssItemTree, 
                                                  &itemData->pubTime );

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
             item && !threadAbort ; 
             item = BalancedBTreeFindLeast( rssItemTree->root ) ) {

            itemData = (RssItem_t *)item->item;
            feed    = itemData->feed;
            pubTime = itemData->pubTime;

            localtime_r( &pubTime, &tm );
            strftime( buf, sizeof(buf), "%H:%M %d %b %Y %z (%Z)", &tm );
            sprintf( message, "[%s] at %s \"%s\"", feed->prefix, buf,
                              itemData->title );
            if( itemData->link ) {
                if( strlen(message) + strlen(itemData->link) + 3  <=
                    feed->server->floodMaxLine ) {
                    sprintf( buf, " (%s)", itemData->link );
                    strcat( message, buf );
                } else {
                    LoggedChannelMessage( feed->server, feed->channel, 
                                          message );
                    LogPrint( LOG_NOTICE, "RSS: feed %d: (%d) %s", 
                              feed->feedId, strlen(message), message );
                    sprintf( message, "  (%s)", itemData->link );
                }
            }

            LoggedChannelMessage( feed->server, feed->channel, message );
            LogPrint( LOG_NOTICE, "RSS: feed %d: (%d) %s", feed->feedId, 
                      strlen(message), message );

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

        gettimeofday( &now, NULL );
        ts.tv_sec  = now.tv_sec + delta;
        ts.tv_nsec = now.tv_usec * 1000;

        pthread_mutex_lock( &signalMutex );
        retval = pthread_cond_timedwait( &kickCond, &signalMutex, &ts );
        pthread_mutex_unlock( &signalMutex );

        if( retval != ETIMEDOUT ) {
            LogPrintNoArg( LOG_NOTICE, "RSS: thread woken up early" );
        }
    }

    LogPrintNoArg( LOG_NOTICE, "Shutting down RSSfeed thread" );
    pthread_mutex_unlock( &shutdownMutex );
    return( NULL );
}

void botCmdRssfeed( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag )
{
    static char            *notauth = "You are not authorized, you can't do "
                                      "that!";
    char                   *line;
    char                   *command;
    char                   *message;
    int                     feedNum;
    BalancedBTreeItem_t    *item;
    RssFeed_t              *feed;
    struct timeval          tv;

    command = CommandLineParse( msg, &line );
    if( !command ) {
        return;
    }

    if( !strcmp( command, "list" ) ) {
        BalancedBTreeLock( rssfeedTree );
        if( line && !strcmp( line, "all" ) ) {
            message = botRssfeedDepthFirst( rssfeedTree->root, server, channel,
                                            false );
        } else if ( line && !strcmp( line, "timeout" ) ) {
            message = botRssfeedDump( rssfeedActiveTree->root );
        } else {
            message = botRssfeedDepthFirst( rssfeedTree->root, server, channel,
                                            true );
        }
        BalancedBTreeUnlock( rssfeedTree );
    } else if( !strcmp( command, "show" ) && line ) {
        feedNum = atoi(line);
        item = BalancedBTreeFind( rssfeedTree, &feedNum, UNLOCKED );
        if( !item ) {
            message = strdup( "No such feed!" );
        } else {
            feed = (RssFeed_t *)item->item;
            if( feed->server != server || 
                (channel && feed->channel != channel) ) {
                message = strdup( "No such feed in this context!" );
            } else {
                message = rssfeedShowDetails( feed );
            }
        }
    } else {
        /* Private message only */
        if( channel ) {
            free( command );
            return;
        }

        if( !authenticate_check( server, who ) ) {
            transmitMsg( server, TX_PRIVMSG, who, notauth);
            free( command );
            return;
        }
        
        if( !strcmp( command, "enable" ) && line ) {
            feedNum = atoi(line);
            item = BalancedBTreeFind( rssfeedTree, &feedNum, UNLOCKED );
            if( !item ) {
                message = strdup( "No such feed!" );
            } else {
                feed = (RssFeed_t *)item->item;

                message = (char *)malloc(strlen(feed->channel->channel) +
                                         strlen(feed->prefix) + 32);
                if( !feed->enabled ) {
                    feed->enabled = TRUE;
                    item = (BalancedBTreeItem_t *)
                               malloc(sizeof(BalancedBTreeItem_t));
                    item->item = (void *)feed;
                    gettimeofday( &tv, NULL );
                    feed->nextpoll = tv.tv_sec;
                    item->key  = (void *)&feed->nextpoll;

                    /* Adjust the poll time to avoid conflict */
                    BalancedBTreeLock( rssfeedActiveTree );
                    rssfeedFindUnconflictingTime( rssfeedActiveTree, 
                                                  &feed->nextpoll );
                    BalancedBTreeAdd( rssfeedActiveTree, item, LOCKED, TRUE );
                    BalancedBTreeUnlock( rssfeedActiveTree );

                    sprintf( message, "Enabled feed %d - %s on %s", 
                                      feed->feedId, feed->prefix,
                                      feed->channel->channel );
                    LogPrint( LOG_NOTICE, "RSS: %s", message );

                    pthread_mutex_lock( &signalMutex );
                    pthread_cond_broadcast( &kickCond );
                    pthread_mutex_unlock( &signalMutex );
                } else {
                    sprintf( message, "Feed %d already enabled", feed->feedId );
                }
            }
        } else if( !strcmp( command, "disable" ) && line ) {
            feedNum = atoi(line);
            item = BalancedBTreeFind( rssfeedTree, &feedNum, UNLOCKED );
            if( !item ) {
                message = strdup( "No such feed!" );
            } else {
                feed = (RssFeed_t *)item->item;

                message = (char *)malloc(strlen(feed->channel->channel) +
                                         strlen(feed->prefix) + 32);
                if( feed->enabled ) {
                    feed->enabled = FALSE;
                    BalancedBTreeLock( rssfeedActiveTree );
                    item = BalancedBTreeFind( rssfeedActiveTree, 
                                              &feed->nextpoll, LOCKED );
                    BalancedBTreeRemove( rssfeedActiveTree, item, LOCKED, 
                                         TRUE );
                    BalancedBTreeUnlock( rssfeedActiveTree );

                    sprintf( message, "Disabled feed %d - %s on %s", 
                                      feed->feedId, feed->prefix,
                                      feed->channel->channel );
                    LogPrint( LOG_NOTICE, "RSS: %s", message );

                    pthread_mutex_lock( &signalMutex );
                    pthread_cond_broadcast( &kickCond );
                    pthread_mutex_unlock( &signalMutex );
                } else {
                    sprintf( message, "Feed %d already disabled", 
                                      feed->feedId );
                }
            }
        } else {
            message = NULL;
            free( command );
            return;
        }
    }

    if( channel ) {
        LoggedChannelMessage( server, channel, message );
    } else {
        transmitMsg( server, TX_PRIVMSG, who, message);
    }

    free( message );
    free( command );
}

char *botHelpRssfeed( void *tag )
{
    static char *help = "Enable/disable/list/show rssfeed notifications for a "
                        "channel.  Must be authenticated to use "
                        "enable/disable.  "
                        "Syntax: (in channel) rssfeed list | rssfeed show num "
                        " (in privmsg) rssfeed enable num | "
                        "rssfeed disable num | rssfeed list | rssfeed show num";
    
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
        LogPrint( LOG_ERR, "Initializing RSSfeed database to schema version %d",
                  CURRENT_SCHEMA_RSSFEED );
        for( i = 0; i < defSchemaCount; i++ ) {
            db_queue_query( i, defSchema, NULL, 0, NULL, NULL, NULL );
        }
        db_set_setting("dbSchemaRssfeed", "%d", CURRENT_SCHEMA_RSSFEED);
        return( CURRENT_SCHEMA_RSSFEED );
    }

    LogPrint( LOG_ERR, "Upgrading RSSfeed database from schema version %d to "
                       "%d", current, current+1 );
    for( i = 0; schemaUpgrade[current][i].queryPattern; i++ ) {
        db_queue_query( i, schemaUpgrade[current], NULL, 0, NULL, NULL, NULL );
    }

    current++;

    db_set_setting("dbSchemaRssfeed", "%d", current);
    return( current );
}

static void db_load_rssfeeds( void )
{
    pthread_mutex_t        *mutex;

    rssfeedTree = BalancedBTreeCreate( BTREE_KEY_INT );
    rssfeedActiveTree = BalancedBTreeCreate( BTREE_KEY_INT );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, rssfeedQueryTable, NULL, 0, result_load_rssfeeds,
                    NULL, mutex );
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );
}

static void result_load_rssfeeds( MYSQL_RES *res, MYSQL_BIND *input, 
                                  void *args )
{
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    RssFeed_t              *data;
    BalancedBTreeItem_t    *item;
    struct timeval          tv;
    time_t                  nextpoll;
    char                   *message;

    if( !res || !(count = mysql_num_rows(res)) ) {
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
        data->offset   = atol(row[7]);
        data->nextpoll = nextpoll++;
        data->enabled  = TRUE;

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)data;
        item->key  = (void *)&data->feedId;
        BalancedBTreeAdd( rssfeedTree, item, LOCKED, FALSE );

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)data;
        item->key  = (void *)&data->nextpoll;

        /* Adjust the poll time to avoid conflict */
        rssfeedFindUnconflictingTime( rssfeedActiveTree, &data->nextpoll );
        BalancedBTreeAdd( rssfeedActiveTree, item, LOCKED, FALSE );
        LogPrint( LOG_NOTICE, "RSS: Loaded %d (%s): server %d, channel %d, "
                              "timeout %d", data->feedId, data->prefix, 
                              data->serverId, data->chanId, data->timeout );
    }

    BalancedBTreeAdd( rssfeedTree, NULL, LOCKED, TRUE );
    BalancedBTreeAdd( rssfeedActiveTree, NULL, LOCKED, TRUE );

    message = botRssfeedDump( rssfeedActiveTree->root );
    LogPrint( LOG_NOTICE, "RSS: %s", message );
    free( message );

    BalancedBTreeUnlock( rssfeedTree );
    BalancedBTreeUnlock( rssfeedActiveTree );
}

void db_update_lastpost( int feedId, int lastPost ) {
    MYSQL_BIND         *data;

    data = (MYSQL_BIND *)malloc(2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], lastPost, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], feedId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "RSS: feed %d: updating lastpost to %d", feedId,
                          lastPost );
    db_queue_query( 1, rssfeedQueryTable, data, 2, NULL, NULL, NULL );
}

void rssfeedFindUnconflictingTime( BalancedBTree_t *tree, time_t *key )
{
    BalancedBTreeItem_t    *item;

    /* Assumes that the tree is already locked */
    for( item = BalancedBTreeFind( tree, key, LOCKED) ; item ;
         item = BalancedBTreeFind( tree, key, LOCKED) ) {
        (*key)++;
    }
}

char *botRssfeedDepthFirst( BalancedBTreeItem_t *item, IRCServer_t *server,
                            IRCChannel_t *channel, bool filter )
{
    static char buf[256];
    char       *message;
    char       *oldmsg;
    char       *submsg;
    int         len;
    RssFeed_t  *feed;

    message = NULL;

    if( !item || !server ) {
        return( message );
    }

    submsg = botRssfeedDepthFirst( item->left, server, channel, filter );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    feed = (RssFeed_t *)item->item;
    if( (server == feed->server) && (!filter || feed->enabled) && 
        (!channel || channel == feed->channel) ) {
        if( !channel ) {
            sprintf( buf, "%d-(%s)-%s", feed->feedId, feed->channel->channel, 
                                        feed->prefix );
        } else {
            sprintf( buf, "%d-%s", feed->feedId, feed->prefix );
        }

        submsg = buf;
        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        if( oldmsg ) {
            strcat( message, ", " );
        } else {
            message[0] = '\0';
        }
        strcat( message, submsg );
    }

    submsg = botRssfeedDepthFirst( item->right, server, channel, filter );
    if( submsg ) {
        len = strlen( message );

        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        strcat( message, ", " );
        strcat( message, submsg );
        free( submsg );
    }

    return( message );
}

char *botRssfeedDump( BalancedBTreeItem_t *item )
{
    static char     buf[256];
    char           *message;
    char           *oldmsg;
    char           *submsg;
    int             len;
    RssFeed_t      *feed;
    struct timeval  now;

    message = NULL;

    if( !item ) {
        return( message );
    }

    submsg = botRssfeedDump( item->left );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    gettimeofday( &now, NULL );
    feed = (RssFeed_t *)item->item;
    sprintf( buf, "%d-%s(%ld/%ld)", feed->feedId, feed->prefix, feed->nextpoll,
             feed->nextpoll - now.tv_sec );

    submsg = buf;
    message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
    if( oldmsg ) {
        strcat( message, ", " );
    } else {
        message[0] = '\0';
    }
    strcat( message, submsg );

    submsg = botRssfeedDump( item->right );
    if( submsg ) {
        len = strlen( message );

        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        strcat( message, ", " );
        strcat( message, submsg );
        free( submsg );
    }

    return( message );
}

char *rssfeedShowDetails( RssFeed_t *feed )
{
    char           *message;
    char            buf[1024];
    char            date[32];
    struct tm       tm;

    localtime_r(&feed->lastPost, &tm);
    strftime(date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm);

    sprintf( buf, "RSS: feed %d%s: prefix [%s], channel %s, URL \"%s\", poll "
                  "interval %lds, offset %lds, last post %s, next poll ", 
                  feed->feedId, (feed->enabled ? "" : " (disabled)"), 
                  feed->prefix, feed->channel->channel, feed->url, 
                  feed->timeout, feed->offset,
                  (feed->lastPost == 0 ? "never" : date) );
    
    localtime_r(&feed->nextpoll, &tm);
    strftime(date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm);
    strcat( buf, date );

    message = strdup(buf);
    return( message );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
