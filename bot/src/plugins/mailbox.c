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
#include <c-client/c-client.h>



/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#define CURRENT_SCHEMA_MAILBOX  2
#define MAX_SCHEMA_QUERY 100

typedef struct {
    int                 mailboxId;
    char               *server;
    int                 port;
    char               *user;
    char               *password;
    char               *protocol;
    char               *options;
    char               *mailbox;
    int                 interval;
    int                 lastCheck;
    int                 lastRead;
    time_t              nextPoll;
    bool                enabled;
    char               *serverSpec;
    int                 newMessages;
    int                 recentMessages;
    int                 totalMessages;
    LinkedList_t       *messageList;
    NETMBX              netmbx;
    MAILSTREAM         *stream;
    LinkedList_t       *reports;
} Mailbox_t;

typedef struct {
    LinkedListItem_t    linkage;
    int                 serverId;
    int                 channelId;
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    char               *nick;
    char               *format;
} MailboxReport_t;

typedef struct {
    LinkedListItem_t    linkage;
    unsigned long       uid;
} MailboxUID_t;

typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_mailbox` (\n"
    "    `mailboxId` INT NULL AUTO_INCREMENT PRIMARY KEY ,\n"
    "    `server` VARCHAR( 255 ) NOT NULL ,\n"
    "    `user` VARCHAR( 255 ) NOT NULL ,\n"
    "    `port` INT NOT NULL DEFAULT '0',\n"
    "    `password` VARCHAR( 255 ) NOT NULL ,\n"
    "    `protocol` VARCHAR( 32 ) NOT NULL ,\n"
    "    `options` VARCHAR( 255 ) NOT NULL ,\n"
    "    `mailbox` VARCHAR( 255 ) NOT NULL ,\n"
    "    `pollInterval` INT NOT NULL DEFAULT '600',\n"
    "    `lastCheck` INT NOT NULL DEFAULT '0',\n"
    "    `lastRead` INT NOT NULL DEFAULT '0'\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE },
  { "CREATE TABLE `plugin_mailbox_report` (\n"
    "  `mailboxId` INT NOT NULL ,\n"
    "  `channelId` INT NOT NULL ,\n"
    "  `serverId` INT NOT NULL ,\n"
    "  `nick` VARCHAR( 64 ) NOT NULL ,\n"
    "  `format` TEXT NOT NULL\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_MAILBOX] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } },
    /* 1 -> 2 */
    { { "CREATE TABLE `plugin_mailbox_report` (\n"
        "  `mailboxId` INT NOT NULL ,\n"
        "  `channelId` INT NOT NULL ,\n"
        "  `serverId` INT NOT NULL ,\n"
        "  `nick` VARCHAR( 64 ) NOT NULL ,\n"
        "  `format` TEXT NOT NULL\n"
        ") TYPE = MYISAM\n", NULL, NULL, FALSE } }
};

static QueryTable_t mailboxQueryTable[] = {
    /* 0 */
    { "SELECT mailboxId, server, port, user, password, protocol, options, "
      "mailbox, pollInterval, lastCheck, lastRead FROM `plugin_mailbox` "
      "ORDER BY `mailboxId` ASC", NULL, NULL, FALSE },
    /* 1 */
    { "UPDATE `plugin_mailbox` SET `lastCheck` = ? WHERE `mailboxId` = ?", 
      NULL, NULL, FALSE },
    /* 2 */
    { "SELECT channelId, serverId, nick, format FROM `plugin_mailbox_report` "
      "WHERE mailboxId = ?", NULL, NULL, FALSE }
};


/* INTERNAL FUNCTION PROTOTYPES */
void botCmdMailbox( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag );
char *botHelpMailbox( void *tag );
void *mailbox_thread(void *arg);
static int db_upgrade_schema( int current, int goal );
static void db_load_mailboxes( void );
void db_update_lastpoll( int mailboxId, int lastPoll );
static void db_load_reports( void );
static void result_load_mailboxes( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args );
static void result_load_reports( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args );
void mailboxFindUnconflictingTime( BalancedBTree_t *tree, time_t *key );
char *botMailboxDump( BalancedBTreeItem_t *item );
Mailbox_t *mailboxFindByNetmbx( NETMBX *netmbx );
Mailbox_t *mailboxFindByStream( MAILSTREAM *stream );
BalancedBTreeItem_t *mailboxRecurseFindNetmbx( BalancedBTreeItem_t *node, 
                                               NETMBX *netmbx );
static void dbRecurseReports( BalancedBTreeItem_t *node, 
                              pthread_mutex_t *mutex );
void mailboxReport( Mailbox_t *mailbox, MailboxReport_t *report );
char *mailboxReportExpand( char *format, ENVELOPE *envelope, BODY *body );


/* INTERNAL VARIABLES  */
pthread_t               mailboxThreadId;
static bool             threadAbort = FALSE;
static pthread_mutex_t  shutdownMutex;
static pthread_mutex_t  signalMutex;
static pthread_cond_t   kickCond;
BalancedBTree_t        *mailboxTree;
BalancedBTree_t        *mailboxActiveTree;
BalancedBTree_t        *mailboxStreamTree;


void plugin_initialize( char *args )
{
    static char            *command = "mailbox";
    char                   *verString;
    int                     ver;
    int                     printed;

    LogPrintNoArg( LOG_NOTICE, "Initializing mailbox..." );

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting("dbSchemaMailbox");
        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current Mailbox database schema version %d", 
                                ver );
            LogPrint( LOG_CRIT, "Code supports version %d", 
                                CURRENT_SCHEMA_MAILBOX );
            printed = TRUE;
        }

        if( ver < CURRENT_SCHEMA_MAILBOX ) {
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA_MAILBOX );
        }
    } while( ver < CURRENT_SCHEMA_MAILBOX );

    pthread_mutex_init( &shutdownMutex, NULL );
    pthread_mutex_init( &signalMutex, NULL );
    pthread_cond_init( &kickCond, NULL );

    thread_create( &mailboxThreadId, mailbox_thread, NULL, "thread_mailbox" );
    botCmd_add( (const char **)&command, botCmdMailbox, botHelpMailbox, NULL );
}

void plugin_shutdown( void )
{
    Mailbox_t              *mailbox;
    BalancedBTreeItem_t    *item;
    MailboxReport_t        *report;

    LogPrintNoArg( LOG_NOTICE, "Removing mailbox..." );
    botCmd_remove( "mailbox" );

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

    /* Need to free the items too! */
    BalancedBTreeLock( mailboxStreamTree );
    BalancedBTreeDestroy( mailboxStreamTree );

    BalancedBTreeLock( mailboxActiveTree );
    while( mailboxActiveTree->root ) {
        item = mailboxActiveTree->root;
        mailbox = (Mailbox_t *)item->item;
        mail_close( mailbox->stream );
        BalancedBTreeRemove( mailboxActiveTree, item, LOCKED, FALSE );
        free( item );
    }
    BalancedBTreeDestroy( mailboxActiveTree );

    BalancedBTreeLock( mailboxTree );
    while( mailboxTree->root ) {
        item = mailboxTree->root;
        mailbox = (Mailbox_t *)item->item;
        free( mailbox->server );
        free( mailbox->user );
        free( mailbox->password );
        free( mailbox->protocol );
        free( mailbox->options );
        free( mailbox->mailbox );
        free( mailbox->serverSpec );

        LinkedListLock( mailbox->reports );
        while( mailbox->reports->head ) {
            report = (MailboxReport_t *)mailbox->reports->head;
            free( report->nick );
            free( report->format );
            LinkedListRemove( mailbox->reports, (LinkedListItem_t *)report,
                              LOCKED );
            free( report );
        }
        LinkedListDestroy( mailbox->reports );

        BalancedBTreeRemove( mailboxTree, item, LOCKED, FALSE );
        free( item );
    }
    BalancedBTreeDestroy( mailboxTree );

    thread_deregister( mailboxThreadId );
}

void *mailbox_thread(void *arg)
{
    time_t                  nextpoll;
    struct timeval          now;
    time_t                  delta;
    BalancedBTreeItem_t    *item;
    LinkedListItem_t       *listItem, *rptItem;
    Mailbox_t              *mailbox;
    struct tm               tm;
    int                     retval;
    bool                    done;
    long                    localoffset;
    struct timespec         ts;
    MailboxReport_t        *report;
    bool                    found;
    IRCServer_t            *server;
    SEARCHPGM               searchProgram;
    static char             sequence[200];
    MailboxUID_t           *msg;

    pthread_mutex_lock( &shutdownMutex );
    db_thread_init();

    LogPrintNoArg( LOG_NOTICE, "Starting Mailbox thread" );

    memset( &searchProgram, 0x00, sizeof(SEARCHPGM) );
    searchProgram.unseen = 1;

    /* Include the c-client library initialization */
    #include <c-client/linkage.c>

    db_load_mailboxes();
    db_load_reports();

    sleep(5);

    while( !GlobalAbort && !threadAbort ) {
        BalancedBTreeLock( mailboxActiveTree );
        item = BalancedBTreeFindLeast( mailboxActiveTree->root );
        BalancedBTreeUnlock( mailboxActiveTree );

        gettimeofday( &now, NULL );
        localtime_r( &now.tv_sec, &tm );
        localoffset = tm.tm_gmtoff;

        delta = 60;
        if( !item ) {
            /* Nothing configured to be active, check in 15min */
            delta = 900;
            goto DelayPoll;
        } 
        
        mailbox = (Mailbox_t *)item->item;
        nextpoll = mailbox->nextPoll;
        if( nextpoll > now.tv_sec + 15 || !ServerList ) {
            delta = nextpoll - now.tv_sec;
            goto DelayPoll;
        }

        /* Trigger all mailboxes expired or to expire in <= 15s */
        BalancedBTreeLock( mailboxActiveTree );
        for( done = FALSE; item && !done && !threadAbort ; 
             item = BalancedBTreeFindLeast( mailboxActiveTree->root ) ) {
            gettimeofday( &now, NULL );
            mailbox = (Mailbox_t *)item->item;
            LogPrint( LOG_NOTICE, "Mailbox: mailbox %d poll in %lds", 
                                  mailbox->mailboxId,
                                  mailbox->nextPoll - now.tv_sec );
            if( mailbox->nextPoll > now.tv_sec + 15 ) {
                delta = mailbox->nextPoll - now.tv_sec;
                done = TRUE;
                continue;
            }

            /* This mailbox needs to be polled now!   Remove it, and requeue it
             * in the tree, then poll it
             */
            LogPrint( LOG_NOTICE, "Mailbox: polling mailbox %d (%s)", 
                                  mailbox->mailboxId, mailbox->serverSpec );

            BalancedBTreeRemove( mailboxActiveTree, item, LOCKED, FALSE );

            /* Adjust the poll time to avoid conflict */
            mailbox->nextPoll = now.tv_sec + mailbox->interval;
            mailboxFindUnconflictingTime( mailboxActiveTree, 
                                          &mailbox->nextPoll );
            BalancedBTreeAdd( mailboxActiveTree, item, LOCKED, FALSE );

            db_update_lastpoll( mailbox->mailboxId, now.tv_sec );

            if( !mail_ping( mailbox->stream ) ) {
                mail_open( mailbox->stream, mailbox->serverSpec, 0 );
            }
            mail_status( mailbox->stream, mailbox->serverSpec, 
                         SA_MESSAGES | SA_UNSEEN );

            if( mailbox->newMessages ) {
                if( !mailbox->messageList ) {
                    mailbox->messageList = LinkedListCreate();
                }
                mail_search_full( mailbox->stream, NULL, &searchProgram, 
                                  SE_UID );

                LinkedListLock( mailbox->reports );
                for( rptItem = mailbox->reports->head; rptItem; 
                     rptItem = rptItem->next ) {
                    report = (MailboxReport_t *)rptItem;

                    /* 
                     * If the server info isn't initialized, 
                     * but is ready to be... 
                     */
                    if( !report->server && ServerList ) {
                        LinkedListLock( ServerList );
                        for( listItem = ServerList->head, found = FALSE;
                             listItem && !found; listItem = listItem->next ) {
                            server = (IRCServer_t *)listItem;
                            if( server->serverId == report->serverId ) {
                                found = TRUE;
                                report->server = server;
                            }
                        }
                        LinkedListUnlock( ServerList );

                        if( report->channelId > 0 ) {
                            report->channel = 
                                FindChannelNum( report->server, 
                                                report->channelId );
                        }
                    }

                    if( !report->server ) {
                        continue;
                    }

                    /* Do the report! */
                    mailboxReport( mailbox, report );
                }
                LinkedListUnlock( mailbox->reports );
            }

            LinkedListLock( mailbox->messageList );
            while( mailbox->messageList->head ) {
                listItem = mailbox->messageList->head;
                msg = (MailboxUID_t *)listItem;

                snprintf( sequence, 200, "%ld", msg->uid );
                mail_setflag_full( mailbox->stream, sequence, "\\Seen", 
                                   ST_UID );

                LinkedListRemove( mailbox->messageList, listItem, LOCKED );
                free( listItem );
            }
            LinkedListUnlock( mailbox->messageList );
        }

        /* Rebalance the trees */
        BalancedBTreeAdd( mailboxActiveTree, NULL, LOCKED, TRUE );
        BalancedBTreeUnlock( mailboxActiveTree );

    DelayPoll:
        LogPrint( LOG_NOTICE, "Mailbox: sleeping for %ds", delta );

        gettimeofday( &now, NULL );
        ts.tv_sec  = now.tv_sec + delta;
        ts.tv_nsec = now.tv_usec * 1000;

        pthread_mutex_lock( &signalMutex );
        retval = pthread_cond_timedwait( &kickCond, &signalMutex, &ts );
        pthread_mutex_unlock( &signalMutex );

        if( retval != ETIMEDOUT ) {
            LogPrintNoArg( LOG_NOTICE, "Mailbox: thread woken up early" );
        }
    }

    LogPrintNoArg( LOG_NOTICE, "Shutting down Mailbox thread" );
    pthread_mutex_unlock( &shutdownMutex );
    return( NULL );
}

void botCmdMailbox( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag )
{
}

char *botHelpMailbox( void *tag )
{
    return( NULL );
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
        LogPrint( LOG_ERR, "Initializing Mailbox database to schema version %d",
                  CURRENT_SCHEMA_MAILBOX );
        for( i = 0; i < defSchemaCount; i++ ) {
            db_queue_query( i, defSchema, NULL, 0, NULL, NULL, NULL );
        }
        db_set_setting("dbSchemaMailbox", "%d", CURRENT_SCHEMA_MAILBOX);
        return( CURRENT_SCHEMA_MAILBOX );
    }

    LogPrint( LOG_ERR, "Upgrading Mailbox database from schema version %d to "
                       "%d", current, current+1 );
    for( i = 0; schemaUpgrade[current][i].queryPattern; i++ ) {
        db_queue_query( i, schemaUpgrade[current], NULL, 0, NULL, NULL, NULL );
    }

    current++;

    db_set_setting("dbSchemaMailbox", "%d", current);
    return( current );
}

static void db_load_mailboxes( void )
{
    pthread_mutex_t        *mutex;

    mailboxTree       = BalancedBTreeCreate( BTREE_KEY_INT );
    mailboxActiveTree = BalancedBTreeCreate( BTREE_KEY_INT );
    mailboxStreamTree = BalancedBTreeCreate( BTREE_KEY_STRING );

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, mailboxQueryTable, NULL, 0, result_load_mailboxes,
                    NULL, mutex );
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );
}

void db_update_lastpoll( int mailboxId, int lastPoll )
{
    MYSQL_BIND         *data;

    data = (MYSQL_BIND *)malloc(2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], lastPoll, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], mailboxId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "Mailbox: mailbox %d: updating lastpoll to %d", 
                          mailboxId, lastPoll );
    db_queue_query( 1, mailboxQueryTable, data, 2, NULL, NULL, NULL );
}

static void db_load_reports( void )
{
    pthread_mutex_t        *mutex;

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    BalancedBTreeLock( mailboxActiveTree );
    dbRecurseReports( mailboxActiveTree->root, mutex );
    BalancedBTreeUnlock( mailboxActiveTree );

    pthread_mutex_destroy( mutex );
    free( mutex );
}

static void dbRecurseReports( BalancedBTreeItem_t *node, 
                              pthread_mutex_t *mutex )
{
    Mailbox_t              *mailbox;
    MYSQL_BIND             *data;

    if( !node ) {
        return;
    }
    dbRecurseReports( node->left, mutex );

    mailbox = (Mailbox_t *)node->item;

    mailbox->reports = LinkedListCreate();

    data = (MYSQL_BIND *)malloc(1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], mailbox->mailboxId, MYSQL_TYPE_LONG );

    db_queue_query( 2, mailboxQueryTable, data, 1, result_load_reports,
                    mailbox, mutex );
    pthread_mutex_unlock( mutex );

    dbRecurseReports( node->right, mutex );
}


static void result_load_mailboxes( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args )
{
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    BalancedBTreeItem_t    *item;
    struct timeval          tv;
    time_t                  nextpoll;
    char                   *message;
    Mailbox_t              *mailbox;
    int                     len;
    char                    port[32], user[300], service[64];

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    gettimeofday( &tv, NULL );
    nextpoll = tv.tv_sec;

    BalancedBTreeLock( mailboxTree );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        mailbox = (Mailbox_t *)malloc(sizeof(Mailbox_t));
        memset( mailbox, 0x00, sizeof(Mailbox_t) );

        mailbox->mailboxId = atoi(row[0]);
        mailbox->server    = strdup(row[1]);
        mailbox->port      = atoi(row[2]);
        mailbox->user      = strdup(row[3]);
        mailbox->password  = strdup(row[4]);
        mailbox->protocol  = strdup(row[5]);
        mailbox->options   = strdup(row[6]);
        mailbox->mailbox   = ( *row[7] ? strdup(row[7]) : strdup("INBOX") );
        mailbox->interval  = atoi(row[8]);
        mailbox->lastCheck = atol(row[9]);
        mailbox->lastRead  = atol(row[10]);
        if( mailbox->lastCheck + mailbox->interval <= nextpoll + 1 ) {
            mailbox->nextPoll  = nextpoll++;
        } else {
            mailbox->nextPoll = mailbox->lastCheck + mailbox->interval;
        }
        mailbox->enabled   = TRUE;
        len = strlen(mailbox->server) + strlen(mailbox->options) + 
              strlen(mailbox->mailbox) + 4;

        if( mailbox->port ) {
            sprintf( port, ":%d", mailbox->port );
        } else {
            port[0] = '\0';
        }

        sprintf( service, "/service=%s", mailbox->protocol );
        sprintf( user, "/user=%s", mailbox->user );
        
        len += strlen( port ) + strlen( service ) + strlen( user );
        mailbox->serverSpec = (char *)malloc(len);
        snprintf( mailbox->serverSpec, len, "{%s%s%s%s%s}%s", mailbox->server,
                  port, user, service, mailbox->options, mailbox->mailbox );

        /* Store by ID */
        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)mailbox;
        item->key  = (void *)&mailbox->mailboxId;
        BalancedBTreeAdd( mailboxTree, item, LOCKED, FALSE );

        /* Setup the next poll */
        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)mailbox;
        item->key  = (void *)&mailbox->nextPoll;

        /* Adjust the poll time to avoid conflict */
        BalancedBTreeLock( mailboxActiveTree );
        mailboxFindUnconflictingTime( mailboxActiveTree, &mailbox->nextPoll );
        BalancedBTreeAdd( mailboxActiveTree, item, LOCKED, FALSE );
        BalancedBTreeUnlock( mailboxActiveTree );
        LogPrint( LOG_NOTICE, "Mailbox: Loaded %d: server %s, user %s, "
                              "interval %d", mailbox->mailboxId,
                              mailbox->server, mailbox->user, 
                              mailbox->interval );

        /* Set up the NETMBX structure */
        mail_valid_net_parse( mailbox->serverSpec, &mailbox->netmbx );

        /* Setup the MAILSTREAM structure */
        mailbox->stream = mail_open( NULL, mailbox->serverSpec, 0 );

        if( mailbox->stream ) {
            item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
            item->item = (void *)mailbox;
            item->key  = (void *)&mailbox->stream->mailbox;
            BalancedBTreeAdd( mailboxStreamTree, item, UNLOCKED, FALSE );
        } else {
            LogPrint( LOG_CRIT, "Mailbox: Mailbox %d failed", 
                                mailbox->mailboxId );
            mailbox->enabled = FALSE;
        }
    }

    BalancedBTreeAdd( mailboxTree, NULL, LOCKED, TRUE );
    BalancedBTreeAdd( mailboxStreamTree, NULL, UNLOCKED, TRUE );

    BalancedBTreeLock( mailboxActiveTree );
    BalancedBTreeAdd( mailboxActiveTree, NULL, LOCKED, TRUE );
    message = botMailboxDump( mailboxActiveTree->root );
    LogPrint( LOG_NOTICE, "Mailbox: %s", message );
    free( message );
    BalancedBTreeUnlock( mailboxActiveTree );

    BalancedBTreeUnlock( mailboxTree );
}

static void result_load_reports( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args )
{
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    Mailbox_t              *mailbox;
    MailboxReport_t        *report;

    mailbox = (Mailbox_t *)args;
    if( !mailbox ) {
        return;
    }

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    LinkedListLock( mailbox->reports );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        report = (MailboxReport_t *)malloc(sizeof(MailboxReport_t));
        memset( report, 0x00, sizeof(MailboxReport_t) );

        report->channelId = atoi(row[0]);
        report->serverId  = atoi(row[1]);
        report->nick      = strdup(row[2]);
        report->format    = strdup(row[3]);

        LinkedListAdd( mailbox->reports, (LinkedListItem_t *)report, LOCKED,
                       AT_TAIL );
    }

    LinkedListUnlock( mailbox->reports );
}

void mailboxFindUnconflictingTime( BalancedBTree_t *tree, time_t *key )
{
    BalancedBTreeItem_t    *item;

    /* Assumes that the tree is already locked */
    for( item = BalancedBTreeFind( tree, key, LOCKED) ; item ;
         item = BalancedBTreeFind( tree, key, LOCKED) ) {
        (*key)++;
    }
}

char *botMailboxDump( BalancedBTreeItem_t *item )
{
    static char     buf[256];
    char           *message;
    char           *oldmsg;
    char           *submsg;
    int             len;
    Mailbox_t      *mailbox;
    struct timeval  now;

    message = NULL;

    if( !item ) {
        return( message );
    }

    submsg = botMailboxDump( item->left );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    gettimeofday( &now, NULL );
    mailbox = (Mailbox_t *)item->item;
    sprintf( buf, "%d %s(%ld/%ld)", mailbox->mailboxId, mailbox->serverSpec, 
                  mailbox->nextPoll, mailbox->nextPoll - now.tv_sec );

    submsg = buf;
    message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
    if( oldmsg ) {
        strcat( message, ", " );
    } else {
        message[0] = '\0';
    }
    strcat( message, submsg );

    submsg = botMailboxDump( item->right );
    if( submsg ) {
        len = strlen( message );

        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        strcat( message, ", " );
        strcat( message, submsg );
        free( submsg );
    }

    return( message );
}

BalancedBTreeItem_t *mailboxRecurseFindNetmbx( BalancedBTreeItem_t *node, 
                                               NETMBX *netmbx )
{
    Mailbox_t              *mailbox;
    NETMBX                 *mbx;
    BalancedBTreeItem_t    *item;

    if( !node ) {
        return( NULL );
    }

    mailbox = (Mailbox_t *)node->item;
    mbx = &mailbox->netmbx;
    if( !strcasecmp( mbx->host,    netmbx->host) &&
        !strcasecmp( mbx->user,    netmbx->user) &&
        !strcasecmp( mbx->mailbox, netmbx->mailbox) &&
        !strcasecmp( mbx->service, netmbx->service) &&
        mbx->port == netmbx->port ) {
        /* Found it! */
        return( node );
    }

    item = mailboxRecurseFindNetmbx( node->left, netmbx );
    if( item ) {
        return( item );
    }

    item = mailboxRecurseFindNetmbx( node->right, netmbx );
    return( item );
}

Mailbox_t *mailboxFindByNetmbx( NETMBX *netmbx )
{
    BalancedBTreeItem_t    *item;
    Mailbox_t              *mailbox;

    BalancedBTreeLock( mailboxActiveTree );
    item = mailboxRecurseFindNetmbx( mailboxActiveTree->root, netmbx );
    if( !item ) {
        BalancedBTreeUnlock( mailboxActiveTree );
        return( NULL );
    }

    mailbox = (Mailbox_t *)item->item;
    BalancedBTreeUnlock( mailboxActiveTree );
    return( mailbox );
}

Mailbox_t *mailboxFindByStream( MAILSTREAM *stream )
{
    BalancedBTreeItem_t    *item;
    Mailbox_t              *mailbox;

    item = (BalancedBTreeItem_t *)BalancedBTreeFind( mailboxStreamTree,
                                                     (void *)&stream->mailbox, 
                                                     UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    mailbox = (Mailbox_t *)item->item;
    return( mailbox );
}

void mailboxReport( Mailbox_t *mailbox, MailboxReport_t *report )
{
    MailboxUID_t       *msg;
    LinkedListItem_t   *item;
    ENVELOPE           *envelope;
    BODY               *body;
    char               *message;

    LinkedListLock( mailbox->messageList );

    for( item = mailbox->messageList->head; item; item = item->next ) {
        msg = (MailboxUID_t *)item;

        envelope = mail_fetchstructure_full( mailbox->stream, msg->uid, &body,
                                             FT_UID );

        message = mailboxReportExpand( report->format, envelope, body );

        if( report->channel ) {
            LogPrint( LOG_INFO, "Mailbox %d - %s, UID %08X, \"%s\"", 
                                mailbox->mailboxId, report->channel->fullspec,
                                msg->uid, message );
            LoggedChannelMessage( report->server, report->channel, message );
        } else {
            LogPrint( LOG_INFO, "Mailbox %d - %s@%s:%d->%s, UID %08X, \"%s\"", 
                                mailbox->mailboxId, report->server->nick,
                                report->server->server, report->server->port,
                                report->nick, msg->uid, message );
            transmitMsg( report->server, TX_PRIVMSG, report->nick, message );
        }

        free( message );
    }

    LinkedListUnlock( mailbox->messageList );
}

char *mailboxReportExpand( char *format, ENVELOPE *envelope, BODY *body )
{
    char           *message;
    char           *origmessage;
    char           *fieldEnd;
    char           *field;
    int             len;
    int             offset;

    len = strlen(format);
    message = (char *)malloc(len);
    origmessage = message;

    for( ; *format; ) {
        if( *format != '$' ) {
            *(message++) = *(format++);
        } else {
            format++;
            fieldEnd = strchr( format, '$' );
            if( !fieldEnd ) {
                *(message++) = '$';
            } else {
                *fieldEnd = '\0';
                field = format;
                format = ++fieldEnd;

                if( !strcasecmp( field, "from" ) ) {
                    offset = message - origmessage;
                    len += strlen( envelope->from->mailbox ) + 1 + 
                           strlen( envelope->from->host ) - 6;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strcat( message, envelope->from->mailbox );
                    strcat( message, "@" );
                    strcat( message, envelope->from->host );
                    message += strlen( message );
                } else if( !strcasecmp( field, "subject" ) ) {
                    offset = message - origmessage;
                    len += strlen( envelope->subject ) - 9;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strcat( message, envelope->subject );
                    message += strlen(message);
                } else {
                    *message = '\0';
                    strcat( message, "$" );
                    strcat( message, field );
                    strcat( message, "$" );
                    message += strlen( message );
                }
            }
        }
    }

    *message = '\0';
    return( origmessage );
}

/*
 * Callbacks for the UW c-client library
 */

void mm_flags( MAILSTREAM *stream, unsigned long number )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in flags: %p", stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: Flags changed: %s message %ld",
                        mailbox->serverSpec, number );
}

void mm_status( MAILSTREAM *stream, char *mailbox, MAILSTATUS *status )
{
    Mailbox_t              *mbox;

    mbox = mailboxFindByStream( stream );
    if( !mbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in flags: %p", stream );
        return;
    }

    if( status->flags & SA_MESSAGES ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - Messages %ld",
                            mailbox, status->messages );
        mbox->totalMessages = status->messages;
    }

    if( status->flags & SA_RECENT ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - Recent Messages %ld", 
                            mailbox, status->recent );
        mbox->recentMessages = status->recent;
    }

    if( status->flags & SA_UNSEEN ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - Unseen Messages %ld", 
                            mailbox, status->unseen );

        mbox->newMessages = status->unseen;
    }

    if( status->flags & SA_UIDNEXT ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - Next UID %ld", 
                            mailbox, status->uidnext );
    }

    if( status->flags & SA_UIDVALIDITY ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - UID validity %ld", 
                            mailbox, status->uidvalidity );
    }
}

void mm_searched( MAILSTREAM *stream, unsigned long number )
{
    Mailbox_t      *mailbox;
    MailboxUID_t   *msg;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in searched: %p", 
                            stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: %s found message at UID %08X", 
                        mailbox->serverSpec, number );

    msg = (MailboxUID_t *)malloc(sizeof(MailboxUID_t));
    msg->uid = number;

    LinkedListAdd( mailbox->messageList, (LinkedListItem_t *)msg, UNLOCKED, 
                   AT_TAIL );
}

void mm_exists( MAILSTREAM *stream, unsigned long number )
{
#if 0
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in exists: %p", 
                            stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: %s last message is %ld", 
                        mailbox->serverSpec, number );
#endif
    LogPrint( LOG_CRIT, "Mailbox: %s last message is %ld", 
                        stream->mailbox, number );
}

void mm_expunged( MAILSTREAM *stream, unsigned long number )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in expunged: %p", 
                            stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: %s expunged message %ld", 
                        mailbox->serverSpec, number );
}

void mm_list( MAILSTREAM *stream, int delimiter, char *name, long attributes )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in list: %p", 
                            stream );
        return;
    }

    if( attributes & LATT_NOSELECT ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - mailbox %s is a heirarchy (%c)",
                            mailbox->serverSpec, name, delimiter );
    }

    if( attributes & LATT_NOINFERIORS ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - mailbox %s is a file",
                            mailbox->serverSpec, name );
    }

    if( attributes & LATT_MARKED ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - mailbox %s is marked",
                            mailbox->serverSpec, name );
    }

    if( attributes & LATT_UNMARKED ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - mailbox %s is not marked",
                            mailbox->serverSpec, name );
    }
}

void mm_lsub( MAILSTREAM *stream, int delimiter, char *name, long attributes )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in lsub: %p", 
                            stream );
        return;
    }

    if( attributes & LATT_NOSELECT ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - sub. mailbox %s is a heirarchy (%c)",
                            mailbox->serverSpec, name, delimiter );
    }

    if( attributes & LATT_NOINFERIORS ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - sub. mailbox %s is a file",
                            mailbox->serverSpec, name );
    }

    if( attributes & LATT_MARKED ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - sub. mailbox %s is marked",
                            mailbox->serverSpec, name );
    }

    if( attributes & LATT_UNMARKED ) {
        LogPrint( LOG_CRIT, "Mailbox: %s - sub. mailbox %s is not marked",
                            mailbox->serverSpec, name );
    }
}

void mm_notify( MAILSTREAM *stream, char *string, long errflg )
{
    LogPrint( LOG_INFO, "Mailbox: %s - %s", stream->mailbox, string );
}

void mm_log( char *string, long errflg )
{
    LogPrint( LOG_INFO, "Mailbox: %s", string );
}

void mm_dlog( char *string )
{
    LogPrint( LOG_DEBUG, "Mailbox: DEBUG: %s", string );
}

void mm_login( NETMBX *mb, char *user, char *pwd, long trial )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByNetmbx( mb );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find NETMBX for login: %p", mb );
        user[0] = '\0';
        pwd[0] = '\0';
        return;
    }

    LogPrint( LOG_INFO, "Mailbox: Attempting login (try %d) - %s", trial,
                        mailbox->serverSpec );
    strcpy( user, mailbox->user );
    strcpy( pwd, mailbox->password );
}

void mm_critical( MAILSTREAM *stream )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in critical: %p",
                            stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: Entering critical: %s", mailbox->serverSpec );
}

void mm_nocritical( MAILSTREAM *stream )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in nocritical: %p",
                            stream );
        return;
    }

    LogPrint( LOG_CRIT, "Mailbox: Leaving critical: %s", mailbox->serverSpec );
}

long mm_diskerror( MAILSTREAM *stream, long errcode, long serious )
{
    Mailbox_t      *mailbox;

    mailbox = mailboxFindByStream( stream );
    if( !mailbox ) {
        LogPrint( LOG_CRIT, "Mailbox: can't find stream in diskerror: %p",
                            stream );
        return( 1 );
    }

    LogPrint( LOG_CRIT, "Mailbox: %s had error: %s%s", mailbox->serverSpec,
                        strerror(errcode), (serious ? " (serious!)" : "") );
    return( 0 );
}

void mm_fatal( char *string )
{
    LogPrint( LOG_CRIT, "Mailbox: FATAL: %s", string );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
