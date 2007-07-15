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

#define CURRENT_SCHEMA_MAILBOX  5

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
    time_t              lastCheck;
    time_t              lastRead;
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
    bool                visited;
    bool                modified;
    char               *menuText;
} Mailbox_t;

typedef struct {
    LinkedListItem_t    linkage;
    int                 mailboxId;
    int                 oldMailboxId;
    int                 chanServId;
    int                 oldChanServId;
    int                 serverId;
    IRCServer_t        *server;
    int                 channelId;
    IRCChannel_t       *channel;
    char               *nick;
    char               *oldNick;
    char               *format;
    bool                enabled;
    bool                visited;
    bool                modified;
    char               *menuText;
} MailboxReport_t;

typedef struct {
    LinkedListItem_t    linkage;
    unsigned long       uid;
} MailboxUID_t;


static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_mailbox` (\n"
    "    `mailboxId` INT NULL AUTO_INCREMENT PRIMARY KEY ,\n"
    "    `enabled` INT NOT NULL DEFAULT '1',\n"
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
    "    PRIMARY KEY ( `mailboxId` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE },
  { "CREATE TABLE `plugin_mailbox_report` (\n"
    "  `mailboxId` INT NOT NULL ,\n"
    "  `enabled` INT NOT NULL DEFAULT '1',\n"
    "  `channelId` INT NOT NULL ,\n"
    "  `nick` VARCHAR( 64 ) NOT NULL ,\n"
    "  `format` TEXT NOT NULL\n"
    "  PRIMARY KEY ( `mailboxId` , `channelId` , `nick` )\n"
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
        ") TYPE = MYISAM\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE } },
    /* 2 -> 3 */
    { { "ALTER TABLE `plugin_mailbox` ADD `enabled` INT NOT NULL DEFAULT '1' "
        "AFTER `mailboxId`", NULL, NULL, FALSE },
      { "ALTER TABLE `plugin_mailbox_report` ADD `enabled` INT NOT NULL "
        "DEFAULT '1' AFTER `mailboxId`", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE } },
    /* 3 -> 4 */
    { { "ALTER TABLE `plugin_mailbox_report` DROP PRIMARY KEY ,\n"
        "ADD PRIMARY KEY ( `mailboxId` , `channelId` , `serverId` , `nick` )",
        NULL, NULL, FALSE },
      { "ALTER TABLE `plugin_mailbox` DROP PRIMARY KEY, \n"
        "ADD PRIMARY KEY ( `mailboxId` )", NULL, NULL, FALSE },
      { NULL, NULL, FALSE } },
    /* 4 -> 5 */
    { { "UPDATE `plugin_mailbox_report` SET `channelId` = `serverId` "
        "WHERE `nick` != ''", NULL, NULL, FALSE },
      { "ALTER TABLE `plugin_mailbox_report` DROP PRIMARY KEY, \n"
        "ADD PRIMARY KEY ( `mailboxId`, `channelId`, `nick` ), \n"
        "DROP `serverId", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t mailboxQueryTable[] = {
    /* 0 */
    { "SELECT mailboxId, server, port, user, password, protocol, options, "
      "mailbox, pollInterval, lastCheck, lastRead, enabled "
      "FROM `plugin_mailbox` ORDER BY `mailboxId` ASC", NULL, NULL, FALSE },
    /* 1 */
    { "UPDATE `plugin_mailbox` SET `lastCheck` = ? WHERE `mailboxId` = ?", 
      NULL, NULL, FALSE },
    /* 2 */
    { "SELECT channelId, nick, format, enabled "
      "FROM `plugin_mailbox_report` WHERE mailboxId = ?", NULL, NULL, FALSE },
    /* 3 */
    { "UPDATE `plugin_mailbox` SET `lastRead` = ? WHERE `mailboxId` = ?", 
      NULL, NULL, FALSE },
    /* 4 */
    { "UPDATE `plugin_mailbox` SET `server` = ?, `port` = ?, `user` = ?, "
      "`password` = ?, `protocol` = ?, `options` = ?, `mailbox` = ?, "
      "`pollInterval` = ?, `enabled` = ? WHERE `mailboxId` = ?", NULL, NULL,
      FALSE },
    /* 5 */
    { "UPDATE `plugin_mailbox_report` SET `mailboxId` = ?, `channelId` = ?, "
      "`nick` = ?, `format` = ?, `enabled` = ? "
      "WHERE `mailboxId` = ? AND `channelId` = ? AND `nick` = ?", NULL, NULL, 
      FALSE }
};


/* INTERNAL FUNCTION PROTOTYPES */
void mailboxSighup( int signum, void *arg );
void botCmdMailbox( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag );
char *botHelpMailbox( void *tag );
void *mailbox_thread(void *arg);
static void db_load_mailboxes( void );
void db_update_lastpoll( int mailboxId, int lastPoll );
void db_update_lastread( int mailboxId, int lastRead );
void db_update_mailbox( Mailbox_t *mailbox );
void db_update_report( MailboxReport_t *report );
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
char *mailboxReportExpand( char *format, ENVELOPE *envelope, char *body,
                           unsigned long bodyLen );
char *botMailboxDepthFirst( BalancedBTreeItem_t *item, IRCServer_t *server,
                            IRCChannel_t *channel, bool filter );
char *mailboxShowDetails( Mailbox_t *mailbox );
void mailboxUnvisitTree( BalancedBTreeItem_t *node );
bool mailboxFlushUnvisited( BalancedBTreeItem_t *node );

void mailboxSaveFunc( void *arg, int index, char *string );
void cursesMailboxRevert( void *arg, char *string );
void cursesMailboxDisplay( void *arg );

void mailboxReportSaveFunc( void *arg, int index, char *string );
void cursesMailboxReportRevert( void *arg, char *string );
void cursesMailboxReportDisplay( void *arg );
void mailboxDisableServer( IRCServer_t *server );
void mailboxDisableChannel( IRCChannel_t *channel );
bool mailboxRecurseDisableServer( BalancedBTreeItem_t *node, 
                                  IRCServer_t *server );
bool mailboxRecurseDisableChannel( BalancedBTreeItem_t *node, 
                                   IRCChannel_t *channel );


/* INTERNAL VARIABLES  */
pthread_t               mailboxThreadId;
static bool             threadAbort = FALSE;
static pthread_mutex_t  shutdownMutex;
static pthread_mutex_t  signalMutex;
static pthread_cond_t   kickCond;
BalancedBTree_t        *mailboxTree;
BalancedBTree_t        *mailboxActiveTree;
BalancedBTree_t        *mailboxStreamTree;
static bool             threadReload = FALSE;
int                     mailboxMenuId;
static ThreadCallback_t callbacks;


void plugin_initialize( char *args )
{
    static char            *command = "mailbox";

    LogPrintNoArg( LOG_NOTICE, "Initializing mailbox..." );

    db_check_schema( "dbSchemaMailbox", "Mailbox", CURRENT_SCHEMA_MAILBOX,
                     defSchema, defSchemaCount, schemaUpgrade );

    pthread_mutex_init( &shutdownMutex, NULL );
    pthread_mutex_init( &signalMutex, NULL );
    pthread_cond_init( &kickCond, NULL );

    mailboxMenuId = cursesMenuItemAdd( 1, -1, "Mailbox", NULL, NULL );

    memset( &callbacks, 0, sizeof( ThreadCallback_t ) );
    callbacks.sighupFunc     = mailboxSighup;
    callbacks.serverDisable  = mailboxDisableServer;
    callbacks.channelDisable = mailboxDisableChannel; 
    thread_create( &mailboxThreadId, mailbox_thread, NULL, "thread_mailbox",
                   &callbacks );
    botCmd_add( (const char **)&command, botCmdMailbox, botHelpMailbox, NULL );
}

void plugin_shutdown( void )
{
    Mailbox_t              *mailbox;
    BalancedBTreeItem_t    *item;
    MailboxReport_t        *report;
    MailboxUID_t           *msg;

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

    cursesMenuItemRemove( 1, mailboxMenuId, "Mailbox" );

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
        mailbox->stream = NULL;
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
        cursesMenuItemRemove( 2, mailboxMenuId, mailbox->menuText );
        free( mailbox->menuText );

        if( mailbox->reports ) {
            LinkedListLock( mailbox->reports );
            while( mailbox->reports->head ) {
                report = (MailboxReport_t *)mailbox->reports->head;
                free( report->nick );
                if( report->oldNick ) {
                    free( report->oldNick );
                }
                free( report->format );
                cursesMenuItemRemove( 2, mailboxMenuId, report->menuText );
                free( report->menuText );
                LinkedListRemove( mailbox->reports, (LinkedListItem_t *)report,
                                  LOCKED );
                free( report );
            }
            LinkedListDestroy( mailbox->reports );
        }

        if( mailbox->messageList ) {
            LinkedListLock( mailbox->messageList );
            while( mailbox->messageList->head ) {
                msg = (MailboxUID_t *)mailbox->messageList->head;
                LinkedListRemove( mailbox->messageList, 
                                  (LinkedListItem_t *)msg, LOCKED );
                free( msg );
            }
            LinkedListDestroy( mailbox->messageList );
        }

        BalancedBTreeRemove( mailboxTree, item, LOCKED, FALSE );
        free( item );
        free( mailbox );
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
    struct timespec         ts;
    MailboxReport_t        *report;
    SEARCHPGM               searchProgram;
    static char             sequence[200];
    MailboxUID_t           *msg;

    pthread_mutex_lock( &shutdownMutex );

    LogPrintNoArg( LOG_NOTICE, "Starting Mailbox thread" );

    memset( &searchProgram, 0x00, sizeof(SEARCHPGM) );
    searchProgram.unseen = 1;

    /* Include the c-client library initialization */
    #include <c-client/linkage.c>
    
    mailboxTree       = BalancedBTreeCreate( BTREE_KEY_INT );
    mailboxActiveTree = BalancedBTreeCreate( BTREE_KEY_INT );
    mailboxStreamTree = BalancedBTreeCreate( BTREE_KEY_STRING );

    BalancedBTreeLock( mailboxTree );
    db_load_mailboxes();
    db_load_reports();
    BalancedBTreeUnlock( mailboxTree );

    sleep(5);

    while( !GlobalAbort && !threadAbort ) {
        BalancedBTreeLock( mailboxActiveTree );
        item = BalancedBTreeFindLeast( mailboxActiveTree->root );
        BalancedBTreeUnlock( mailboxActiveTree );

        gettimeofday( &now, NULL );
        localtime_r( &now.tv_sec, &tm );

        delta = 60;
        if( !item ) {
            /* Nothing configured to be active, check in 15min */
            delta = 900;
            goto DelayPoll;
        } 

        if( !ChannelsLoaded ) {
            delta = 60;
            goto DelayPoll;
        }
        
        mailbox = (Mailbox_t *)item->item;
        nextpoll = mailbox->nextPoll;
        if( nextpoll > now.tv_sec + 15 ) {
            delta = nextpoll - now.tv_sec;
            goto DelayPoll;
        }

        /* Trigger all mailboxes expired or to expire in <= 15s */
        BalancedBTreeLock( mailboxActiveTree );
        for( done = FALSE; item && !done && !threadAbort && !GlobalAbort; 
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

            if( GlobalAbort || threadAbort ) {
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

            mailbox->lastCheck = now.tv_sec;
            db_update_lastpoll( mailbox->mailboxId, now.tv_sec );

            if( GlobalAbort || threadAbort ) {
                continue;
            }

            if( !mailbox->stream || !mail_ping( mailbox->stream ) ) {
                mailbox->stream = mail_open( mailbox->stream, 
                                             mailbox->serverSpec, 0 );
            }

            if( GlobalAbort || threadAbort || !mailbox->stream ) {
                continue;
            }

            mail_status( mailbox->stream, mailbox->serverSpec, 
                         SA_MESSAGES | SA_UNSEEN );

            if( mailbox->newMessages ) {
                if( !mailbox->messageList ) {
                    mailbox->messageList = LinkedListCreate();
                }

                if( GlobalAbort || threadAbort ) {
                    continue;
                }

                mail_search_full( mailbox->stream, NULL, &searchProgram, 
                                  SE_UID ); /* | SE_NOPREFETCH */

                LinkedListLock( mailbox->reports );
                for( rptItem = mailbox->reports->head; 
                     rptItem && !GlobalAbort && !threadAbort; 
                     rptItem = rptItem->next ) {
                    report = (MailboxReport_t *)rptItem;

                    /* 
                     * If the server info isn't initialized, 
                     * but is ready to be... 
                     */
                    if( (!report->server || report->serverId == -1) && 
                        ChannelsLoaded ) {
                        if( !strcmp( report->nick, "" ) ) {
                            report->serverId = 
                                FindServerWithChannel( report->channelId );
                        }
                        report->server   = FindServerNum( report->serverId );

                        if( report->channelId > 0 ) {
                            report->channel = 
                                FindChannelNum( report->server, 
                                                report->channelId );
                        }
                    }

                    if( !report->server || !report->enabled ) {
                        continue;
                    }

                    /* Do the report! */
                    if( GlobalAbort || threadAbort ) {
                        continue;
                    }

                    mailboxReport( mailbox, report );

                    gettimeofday( &now, NULL );
                    mailbox->lastRead = now.tv_sec;
                    db_update_lastread( mailbox->mailboxId, now.tv_sec );
                }
                LinkedListUnlock( mailbox->reports );
            }

            if( mailbox->messageList ) {
                LinkedListLock( mailbox->messageList );
                while( mailbox->messageList->head && !GlobalAbort && 
                       !threadAbort ) {
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
        }

        /* Rebalance the trees */
        BalancedBTreeAdd( mailboxActiveTree, NULL, LOCKED, TRUE );
        BalancedBTreeUnlock( mailboxActiveTree );

    DelayPoll:
        LogPrint( LOG_NOTICE, "Mailbox: sleeping for %ds", delta );

        gettimeofday( &now, NULL );
        ts.tv_sec  = now.tv_sec + delta;
        ts.tv_nsec = now.tv_usec * 1000;

        if( delta > 0 && !GlobalAbort && !threadAbort ) {
            pthread_mutex_lock( &signalMutex );
            retval = pthread_cond_timedwait( &kickCond, &signalMutex, &ts );
            pthread_mutex_unlock( &signalMutex );

            if( retval != ETIMEDOUT ) {
                LogPrintNoArg( LOG_NOTICE, "Mailbox: thread woken up early" );
            }
        }

        if( threadReload ) {
            threadReload = FALSE;

            LogPrintNoArg( LOG_NOTICE, "Mailbox thread needs data reload" );

            BalancedBTreeLock( mailboxTree );

            mailboxUnvisitTree( mailboxTree->root );
            db_load_mailboxes();
            db_load_reports();
            while( mailboxFlushUnvisited( mailboxTree->root ) ) {
                /*
                 * Keep calling until nothing was flushed as any flushing 
                 * deletes from the tree which messes up the recursion
                 */
            }

            /* Rebalance trees */
            BalancedBTreeAdd( mailboxTree, NULL, LOCKED, TRUE );
            BalancedBTreeAdd( mailboxActiveTree, NULL, UNLOCKED, TRUE );
            BalancedBTreeAdd( mailboxStreamTree, NULL, UNLOCKED, TRUE );

            BalancedBTreeUnlock( mailboxTree );

            LogPrintNoArg( LOG_NOTICE, "Mailbox thread done data reload" );
        }
    }

    LogPrintNoArg( LOG_NOTICE, "Shutting down Mailbox thread" );
    pthread_mutex_unlock( &shutdownMutex );
    return( NULL );
}

void mailboxUnvisitTree( BalancedBTreeItem_t *node )
{
    Mailbox_t          *mailbox;
    MailboxReport_t    *report;
    LinkedListItem_t   *rptItem;

    if( !node ) {
        return;
    }

    mailboxUnvisitTree( node->left );

    mailbox = (Mailbox_t *)node->item;
    mailbox->visited = FALSE;

    if( mailbox->reports ) {
        LinkedListLock( mailbox->reports );
        for( rptItem = mailbox->reports->head; rptItem; 
             rptItem = rptItem->next ) {
            report = (MailboxReport_t *)rptItem;
            report->visited = FALSE;
        }
        LinkedListUnlock( mailbox->reports );
    }

    mailboxUnvisitTree( node->right );
}

bool mailboxFlushUnvisited( BalancedBTreeItem_t *node )
{
    Mailbox_t              *mailbox;
    MailboxReport_t        *report;
    LinkedListItem_t       *rptItem, *next;
    BalancedBTreeItem_t    *item;
    MailboxUID_t           *msg;
    bool                    found;

    if( !node ) {
        return( FALSE );
    }

    if( mailboxFlushUnvisited( node->left ) ) {
        return( TRUE );
    }

    mailbox = (Mailbox_t *)node->item;
    if( !mailbox->visited ) {
        if( mailbox->enabled ) {
            mail_close( mailbox->stream );
            mailbox->stream = NULL;
        }

        BalancedBTreeRemove( node->btree, node, LOCKED, FALSE );
        item = BalancedBTreeFind( mailboxActiveTree, &mailbox->nextPoll, 
                                  UNLOCKED );
        if( item ) {
            BalancedBTreeRemove( item->btree, item, UNLOCKED, FALSE );
            free( item );
        }

        item = BalancedBTreeFind( mailboxStreamTree, &mailbox->stream->mailbox,
                                  UNLOCKED );
        if( item ) {
            BalancedBTreeRemove( item->btree, item, UNLOCKED, FALSE );
            free( item );
        }

        free( mailbox->server );
        free( mailbox->user );
        free( mailbox->password );
        free( mailbox->protocol );
        free( mailbox->options );
        free( mailbox->mailbox );
        free( mailbox->serverSpec );
        cursesMenuItemRemove( 2, mailboxMenuId, mailbox->menuText );
        free( mailbox->menuText );

        if( mailbox->reports ) {
            LinkedListLock( mailbox->reports );
            while( mailbox->reports->head ) {
                report = (MailboxReport_t *)mailbox->reports->head;
                free( report->nick );
                if( report->oldNick ) {
                    free( report->oldNick );
                }
                free( report->format );
                cursesMenuItemRemove( 2, mailboxMenuId, report->menuText );
                free( report->menuText );
                LinkedListRemove( mailbox->reports, (LinkedListItem_t *)report,
                                  LOCKED );
                free( report );
            }
            LinkedListDestroy( mailbox->reports );
        }

        if( mailbox->messageList ) {
            LinkedListLock( mailbox->messageList );
            while( mailbox->messageList->head ) {
                msg = (MailboxUID_t *)mailbox->messageList->head;
                LinkedListRemove( mailbox->messageList,
                                  (LinkedListItem_t *)msg, LOCKED );
                free( msg );
            }
            LinkedListDestroy( mailbox->messageList );
        }

        free( node );
        free( mailbox );

        return( TRUE );
    }

    if( mailbox->reports ) {
        found = FALSE;

        LinkedListLock( mailbox->reports );
        for( rptItem = mailbox->reports->head; rptItem; rptItem = next ) {
            report = (MailboxReport_t *)rptItem;
            next = rptItem->next;

            if( !report->visited ) {
                free( report->nick );
                if( report->oldNick ) {
                    free( report->oldNick );
                }
                free( report->format );
                cursesMenuItemRemove( 2, mailboxMenuId, report->menuText );
                free( report->menuText );
                LinkedListRemove( mailbox->reports, (LinkedListItem_t *)report,
                                  LOCKED );
                free( report );
                found = TRUE;
            }
        }
        LinkedListUnlock( mailbox->reports );
        if( found ) {
            return( TRUE );
        }
    }

    if( mailboxFlushUnvisited( node->right ) ) {
        return( TRUE );
    }

    return( FALSE );
}

void mailboxSighup( int signum, void *arg )
{
    threadReload = TRUE;

    /* kick the thread */
    pthread_mutex_lock( &signalMutex );
    pthread_cond_broadcast( &kickCond );
    pthread_mutex_unlock( &signalMutex );
}

void botCmdMailbox( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag )
{
    static char            *notauth = "You are not authorized, you can't do "
                                      "that!";
    char                   *line;
    char                   *command;
    char                   *message;
    int                     mailboxNum;
    BalancedBTreeItem_t    *item;
    Mailbox_t              *mailbox;
    struct timeval          tv;

    command = CommandLineParse( msg, &line );
    if( !command ) {
        return;
    }

    if( !strcmp( command, "list" ) ) {
        BalancedBTreeLock( mailboxTree );
        if( line && !strcmp( line, "all" ) ) {
            message = botMailboxDepthFirst( mailboxTree->root, server, channel,
                                            false );
        } else if ( line && !strcmp( line, "timeout" ) ) {
            BalancedBTreeLock( mailboxActiveTree );
            message = botMailboxDump( mailboxActiveTree->root );
            BalancedBTreeUnlock( mailboxActiveTree );
        } else {
            message = botMailboxDepthFirst( mailboxTree->root, server, channel,
                                            true );
        }
        BalancedBTreeUnlock( mailboxTree );
    } else if( !strcmp( command, "show" ) && line ) {
        mailboxNum = atoi(line);
        item = BalancedBTreeFind( mailboxTree, &mailboxNum, UNLOCKED );
        if( !item ) {
            message = strdup( "No such mailbox!" );
        } else {
            mailbox = (Mailbox_t *)item->item;
            message = mailboxShowDetails( mailbox );
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
            mailboxNum = atoi(line);
            item = BalancedBTreeFind( mailboxTree, &mailboxNum, UNLOCKED );
            if( !item ) {
                message = strdup( "No such mailbox!" );
            } else {
                mailbox = (Mailbox_t *)item->item;

                message = (char *)malloc(strlen(mailbox->serverSpec) + 32 );
                if( !mailbox->enabled ) {
                    mailbox->enabled = TRUE;
                    item = (BalancedBTreeItem_t *)
                               malloc(sizeof(BalancedBTreeItem_t));
                    item->item = (void *)mailbox;
                    gettimeofday( &tv, NULL );
                    mailbox->nextPoll = tv.tv_sec;
                    item->key  = (void *)&mailbox->nextPoll;

                    /* Adjust the poll time to avoid conflict */
                    BalancedBTreeLock( mailboxActiveTree );
                    mailboxFindUnconflictingTime( mailboxActiveTree, 
                                                  &mailbox->nextPoll );
                    BalancedBTreeAdd( mailboxActiveTree, item, LOCKED, TRUE );
                    BalancedBTreeUnlock( mailboxActiveTree );

                    sprintf( message, "Enabled mailbox %d - %s", 
                                      mailbox->mailboxId,
                                      mailbox->serverSpec );
                    LogPrint( LOG_NOTICE, "Mailbox: %s", message );

                    pthread_mutex_lock( &signalMutex );
                    pthread_cond_broadcast( &kickCond );
                    pthread_mutex_unlock( &signalMutex );
                } else {
                    sprintf( message, "Mailbox %d already enabled", 
                                      mailbox->mailboxId );
                }
            }
        } else if( !strcmp( command, "disable" ) && line ) {
            mailboxNum = atoi(line);
            item = BalancedBTreeFind( mailboxTree, &mailboxNum, UNLOCKED );
            if( !item ) {
                message = strdup( "No such feed!" );
            } else {
                mailbox = (Mailbox_t *)item->item;

                message = (char *)malloc(strlen(mailbox->serverSpec) + 32 );

                if( mailbox->enabled ) {
                    mailbox->enabled = FALSE;
                    BalancedBTreeLock( mailboxActiveTree );
                    item = BalancedBTreeFind( mailboxActiveTree, 
                                              &mailbox->nextPoll, LOCKED );
                    BalancedBTreeRemove( mailboxActiveTree, item, LOCKED, 
                                         TRUE );
                    BalancedBTreeUnlock( mailboxActiveTree );

                    sprintf( message, "Disabled mailbox %d - %s", 
                                      mailbox->mailboxId, mailbox->serverSpec );
                    LogPrint( LOG_NOTICE, "Mailbox: %s", message );

                    pthread_mutex_lock( &signalMutex );
                    pthread_cond_broadcast( &kickCond );
                    pthread_mutex_unlock( &signalMutex );
                } else {
                    sprintf( message, "Mailbox %d already disabled", 
                                      mailbox->mailboxId );
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

char *botHelpMailbox( void *tag )
{
    static char *help = "Enable/disable/list/show mailbox polling.  "
                        "Must be authenticated to use enable/disable.  "
                        "Syntax: (in channel) mailbox list | mailbox show num "
                        " (in privmsg) mailbox enable num | "
                        "mailbox disable num | mailbox list | mailbox show num";
    
    return( help );
}


static void db_load_mailboxes( void )
{
    pthread_mutex_t        *mutex;

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

    if( !mailbox->reports ) {
        mailbox->reports = LinkedListCreate();
    }

    data = (MYSQL_BIND *)malloc(1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], mailbox->mailboxId, MYSQL_TYPE_LONG );

    db_queue_query( 2, mailboxQueryTable, data, 1, result_load_reports,
                    mailbox, mutex );
    pthread_mutex_unlock( mutex );

    dbRecurseReports( node->right, mutex );
}


void db_update_lastread( int mailboxId, int lastRead )
{
    MYSQL_BIND         *data;

    data = (MYSQL_BIND *)malloc(2 * sizeof(MYSQL_BIND));
    memset( data, 0, 2 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], lastRead, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], mailboxId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "Mailbox: mailbox %d: updating lastread to %d", 
                          mailboxId, lastRead );
    db_queue_query( 3, mailboxQueryTable, data, 2, NULL, NULL, NULL );
}

void db_update_mailbox( Mailbox_t *mailbox )
{
    MYSQL_BIND         *data;

    data = (MYSQL_BIND *)malloc(10 * sizeof(MYSQL_BIND));
    memset( data, 0, 10 * sizeof(MYSQL_BIND) );

    bind_string( &data[0], mailbox->server, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[1], mailbox->port, MYSQL_TYPE_LONG );
    bind_string( &data[2], mailbox->user, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[3], mailbox->password, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[4], mailbox->protocol, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[5], mailbox->options, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[6], mailbox->mailbox, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[7], mailbox->interval, MYSQL_TYPE_LONG );
    bind_numeric( &data[8], mailbox->enabled, MYSQL_TYPE_LONG );
    bind_numeric( &data[9], mailbox->mailboxId, MYSQL_TYPE_LONG );

    LogPrint( LOG_NOTICE, "Mailbox: mailbox %d: updating database", 
                          mailbox->mailboxId );
    db_queue_query( 4, mailboxQueryTable, data, 10, NULL, NULL, NULL );
}

void db_update_report( MailboxReport_t *report )
{
    MYSQL_BIND         *data;

    data = (MYSQL_BIND *)malloc(8 * sizeof(MYSQL_BIND));
    memset( data, 0, 8 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], report->mailboxId, MYSQL_TYPE_LONG );
    bind_numeric( &data[1], report->chanServId, MYSQL_TYPE_LONG );
    bind_string( &data[2], report->nick, MYSQL_TYPE_VAR_STRING );
    bind_string( &data[3], report->format, MYSQL_TYPE_VAR_STRING );
    bind_numeric( &data[4], report->enabled, MYSQL_TYPE_LONG );
    bind_numeric( &data[5], report->oldMailboxId, MYSQL_TYPE_LONG );
    bind_numeric( &data[6], report->oldChanServId, MYSQL_TYPE_LONG );
    bind_string( &data[7], report->oldNick, MYSQL_TYPE_VAR_STRING );

    LogPrintNoArg( LOG_NOTICE, "Mailbox: updating report in database" );
    db_queue_query( 5, mailboxQueryTable, data, 8, NULL, NULL, NULL );
}

/* Assumes the mailboxTree is already locked */
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
    int                     mailboxId;
    bool                    found;
    bool                    oldEnabled;
    char                   *oldServerSpec;
    bool                    newMailbox;
    char                   *menuText;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    gettimeofday( &tv, NULL );
    nextpoll = tv.tv_sec;

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        mailboxId = atoi(row[0]);
        item = BalancedBTreeFind( mailboxTree, &mailboxId, LOCKED );
        if( item ) {
            mailbox = (Mailbox_t *)item->item;
            oldEnabled = mailbox->enabled;
            oldServerSpec = strdup( mailbox->serverSpec );
            found = TRUE;
        } else {
            item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
            mailbox = (Mailbox_t *)malloc(sizeof(Mailbox_t));
            memset( mailbox, 0x00, sizeof(Mailbox_t) );
            oldEnabled = FALSE;
            oldServerSpec = NULL;
            found = FALSE;
        }

        newMailbox = FALSE;
        mailbox->mailboxId = atoi(row[0]);
        
        if( found ) {
            free( mailbox->server );
        }
        mailbox->server    = strdup(row[1]);
        mailbox->port      = atoi(row[2]);

        if( found ) {
            free( mailbox->user );
        }
        mailbox->user      = strdup(row[3]);

        if( found ) {
            free( mailbox->password );
        }
        mailbox->password  = strdup(row[4]);

        if( found ) {
            free( mailbox->protocol );
        }
        mailbox->protocol  = strdup(row[5]);

        if( found ) {
            free( mailbox->options );
        }
        mailbox->options   = strdup(row[6]);

        if( found ) {
            free( mailbox->mailbox );
        }
        mailbox->mailbox   = ( *row[7] ? strdup(row[7]) : strdup("INBOX") );
        mailbox->interval  = atoi(row[8]);
        mailbox->lastCheck = atol(row[9]);
        mailbox->lastRead  = atol(row[10]);
        mailbox->enabled   = ( atoi(row[11]) == 0 ? FALSE : TRUE );
        mailbox->visited   = TRUE;

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
        if( found ) {
            free( mailbox->serverSpec );
        }
        mailbox->serverSpec = (char *)malloc(len);
        snprintf( mailbox->serverSpec, len, "{%s%s%s%s%s}%s", mailbox->server,
                  port, user, service, mailbox->options, mailbox->mailbox );

        len = strlen( mailbox->server ) + strlen( mailbox->user ) + 
              strlen( mailbox->protocol ) + 30;
        menuText = (char *)malloc(len);
        snprintf( menuText, len, "%d - %s@%s (%s)", mailbox->mailboxId,
                            mailbox->user, mailbox->server, mailbox->protocol );
        if( found ) {
            if( strcmp( menuText, mailbox->menuText ) ) {
                cursesMenuItemRemove( 2, mailboxMenuId, mailbox->menuText );
                free( mailbox->menuText );
                mailbox->menuText = menuText;
                cursesMenuItemAdd( 2, mailboxMenuId, mailbox->menuText,
                                   cursesMailboxDisplay, mailbox );
            } else {
                free( menuText );
            }
        } else {
            mailbox->menuText = menuText;
            cursesMenuItemAdd( 2, mailboxMenuId, mailbox->menuText, 
                               cursesMailboxDisplay, mailbox );
        }

        /* Store by ID */
        if( !found ) {
            item->item = (void *)mailbox;
            item->key  = (void *)&mailbox->mailboxId;
            BalancedBTreeAdd( mailboxTree, item, LOCKED, FALSE );
        }

        if( found && strcmp( oldServerSpec, mailbox->serverSpec ) ) {
            newMailbox = TRUE;
        }

        if( !found || ((!oldEnabled && mailbox->enabled) || newMailbox) ||
            mailbox->modified ) {
            if( mailbox->lastCheck + mailbox->interval <= nextpoll + 1 ) {
                mailbox->nextPoll  = nextpoll++;
            } else {
                mailbox->nextPoll = mailbox->lastCheck + mailbox->interval;
            }
        }

        if( found && (((oldEnabled && !mailbox->enabled) || newMailbox) ||
                      mailbox->modified) ) {
            item = BalancedBTreeFind( mailboxActiveTree, &mailbox->nextPoll,
                                      UNLOCKED );
            if( item ) {
                BalancedBTreeRemove( mailboxActiveTree, item, UNLOCKED, FALSE );
                free( item );
            }

            if( mailbox->stream ) {
                item = BalancedBTreeFind( mailboxStreamTree, 
                                          &mailbox->stream->mailbox, UNLOCKED );

                mail_close( mailbox->stream );
                mailbox->stream = NULL;

                if( item ) {
                    BalancedBTreeRemove( mailboxStreamTree, item, UNLOCKED, 
                                         FALSE );
                    free( item );
                }
            }
        }


        if( !mailbox->enabled || 
            (oldEnabled && !newMailbox && !mailbox->modified) ) {
            mailbox->modified = FALSE;
            continue;
        }

        mailbox->modified = FALSE;

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
#if 0
            mailbox->enabled = FALSE;
            BalancedBTreeRemove( mailboxActiveTree, item, LOCKED, FALSE );
#endif
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
}

static void result_load_reports( MYSQL_RES *res, MYSQL_BIND *input, 
                                 void *args )
{
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    Mailbox_t              *mailbox;
    MailboxReport_t        *report;
    LinkedListItem_t       *rptItem;
    bool                    found;
    char                   *menuText;

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

        for( rptItem = mailbox->reports->head, found = FALSE; 
             rptItem && !found; rptItem = rptItem->next ) {
            report = (MailboxReport_t *)rptItem;
            if( report->chanServId == atoi(row[0]) &&
                !strcmp( report->nick, row[1] ) ) {
                found = TRUE;
                break;
            }
        }
        
        if( !found ) {
            report = (MailboxReport_t *)malloc(sizeof(MailboxReport_t));
            memset( report, 0x00, sizeof(MailboxReport_t) );
        }

        report->mailboxId = mailbox->mailboxId;
        if( strcmp( row[1], "" ) ) {
            report->chanServId = atoi(row[0]);
            report->channelId  = 0;
            report->serverId   = report->chanServId;
        } else {
            report->chanServId = atoi(row[0]);
            report->channelId  = report->chanServId;
            report->serverId   = FindServerWithChannel( report->channelId );
        }

        if( found ) {
            free( report->nick );
        }
        report->nick      = strdup(row[1]);

        if( found ) {
            free( report->format );
        }
        report->format    = strdup(row[2]);
        report->enabled   = ( atoi(row[3]) == 0 ? FALSE : TRUE );
        report->visited   = TRUE;
        report->modified  = FALSE;

        menuText = (char *)malloc(64);
        snprintf( menuText, 64, "Report M: %d, S: %d, C: %d", 
                  report->mailboxId, report->serverId, report->channelId );
        if( found ) {
            if( strcmp( menuText, report->menuText ) ) {
                cursesMenuItemRemove( 2, mailboxMenuId, report->menuText );
                free( report->menuText );
                report->menuText = menuText;
                cursesMenuItemAdd( 2, mailboxMenuId, report->menuText,
                                   cursesMailboxReportDisplay, report );
            } else {
                free( menuText );
            }
        } else {
            report->menuText = menuText;
            cursesMenuItemAdd( 2, mailboxMenuId, report->menuText, 
                               cursesMailboxReportDisplay, report );
        }

        if( ChannelsLoaded ) {
            if( !strcmp( report->nick, "" ) ) {
                report->serverId = FindServerWithChannel( report->channelId );
            }
            report->server  = FindServerNum( report->serverId );

            if( report->channelId > 0 ) {
                report->channel = FindChannelNum( report->server, 
                                                  report->channelId );
            }
        } else {
            report->server  = NULL;
            report->channel = NULL;
        }

        report->oldMailboxId = report->mailboxId;
        report->oldChanServId = report->chanServId;
        if( report->oldNick ) {
            free( report->oldNick );
        }
        report->oldNick = strdup( report->nick );

        if( !found ) {
            LinkedListAdd( mailbox->reports, (LinkedListItem_t *)report, 
                           LOCKED, AT_TAIL );
        }
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

char *botMailboxDepthFirst( BalancedBTreeItem_t *item, IRCServer_t *server,
                            IRCChannel_t *channel, bool filter )
{
    static char buf[256];
    char       *message;
    char       *oldmsg;
    char       *submsg;
    int         len;
    Mailbox_t  *mailbox;

    message = NULL;

    if( !item || !server ) {
        return( message );
    }

    submsg = botMailboxDepthFirst( item->left, server, channel, filter );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    mailbox = (Mailbox_t *)item->item;
    if( !filter || mailbox->enabled ) {
        sprintf( buf, "%d-%s", mailbox->mailboxId, mailbox->serverSpec );

        submsg = buf;
        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        if( oldmsg ) {
            strcat( message, ", " );
        } else {
            message[0] = '\0';
        }
        strcat( message, submsg );
    }

    submsg = botMailboxDepthFirst( item->right, server, channel, filter );
    if( submsg ) {
        len = strlen( message );

        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        strcat( message, ", " );
        strcat( message, submsg );
        free( submsg );
    }

    return( message );
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

char *mailboxShowDetails( Mailbox_t *mailbox )
{
    char           *message;
    char            buf[1024];
    char            date[32];
    struct tm       tm;

    localtime_r((const time_t *)&mailbox->lastCheck, &tm);
    strftime(date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm);

    sprintf( buf, "Mailbox: mailbox %d%s: %s, poll interval %ds, last "
                  "checked %s, last read ", 
                  mailbox->mailboxId, (mailbox->enabled ? "" : " (disabled)"), 
                  mailbox->serverSpec, mailbox->interval,
                  (mailbox->lastCheck == 0 ? "never" : date) );

    if( mailbox->lastRead ) {
        localtime_r((const time_t *)&mailbox->lastRead, &tm);
        strftime(date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm);
    } else {
        strcpy( date, "never" );
    }
    strcat( buf, date );

    strcat( buf, ", next poll " );
    localtime_r((const time_t *)&mailbox->nextPoll, &tm);
    strftime(date, 32, "%a, %e %b %Y %H:%M:%S %Z", &tm);
    strcat( buf, date );

    message = strdup(buf);
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
    if( !strcasecmp( mbx->orighost, netmbx->orighost) &&
        !strcasecmp( mbx->user,     netmbx->user) &&
        !strcasecmp( mbx->mailbox,  netmbx->mailbox) &&
        !strcasecmp( mbx->service,  netmbx->service) &&
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
    char               *message;
    char               *body;
    unsigned long       bodyLen;

    LinkedListLock( mailbox->messageList );

    for( item = mailbox->messageList->head; item; item = item->next ) {
        msg = (MailboxUID_t *)item;

        envelope = mail_fetchstructure_full( mailbox->stream, msg->uid, NULL,
                                             FT_UID );

        body = mail_fetchtext_full( mailbox->stream, msg->uid, &bodyLen, 
                                    FT_UID );

        message = mailboxReportExpand( report->format, envelope, body, 
                                       bodyLen );

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

char *mailboxReportExpand( char *format, ENVELOPE *envelope, char *body,
                           unsigned long bodyLen )
{
    char           *message;
    char           *origmessage;
    char           *fieldEnd;
    char           *field;
    int             len;
    int             offset;
    char           *origbody;
    bool            bodyProcessed;
    int             fieldLen;

    bodyProcessed = FALSE;
    len = strlen(format) + 1;
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
                fieldLen = fieldEnd - format;
                field = strndup( format, fieldLen );
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
                } else if( !strcasecmp( field, "to" ) ) {
                    offset = message - origmessage;
                    len += strlen( envelope->to->mailbox ) + 1 + 
                           strlen( envelope->to->host ) - 4;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strcat( message, envelope->to->mailbox );
                    strcat( message, "@" );
                    strcat( message, envelope->to->host );
                    message += strlen( message );
                } else if( !strcasecmp( field, "date" ) ) {
                    offset = message - origmessage;
                    len += strlen( envelope->date ) - 6;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strcat( message, envelope->date );
                    message += strlen(message);
                } else if( !strcasecmp( field, "messageid" ) ) {
                    offset = message - origmessage;
                    len += strlen( envelope->message_id ) - 11;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strcat( message, envelope->message_id );
                    message += strlen(message);
                } else if( !strcasecmp( field, "body" ) ) {
                    if( !bodyProcessed ) {
                        origbody = body;
                        while( *body ) {
                            if( *body == '\n' || *body == '\r' ) {
                                *body = ' ';
                            }
                            body++;
                        }
                        body = origbody;

                        if( bodyLen > 200 ) {
                            bodyLen = 200;
                        }
                        bodyProcessed = TRUE;
                    }

                    offset = message - origmessage;
                    len += bodyLen - 6;
                    origmessage = realloc( origmessage, len );
                    message = origmessage + offset;
                    *message = '\0';
                    strncat( message, body, bodyLen );
                    message += strlen(message);
                } else {
                    *message = '\0';
                    strcat( message, "$" );
                    strcat( message, field );
                    strcat( message, "$" );
                    message += strlen( message );
                }

                free( field );
            }
        }
    }

    *message = '\0';
    return( origmessage );
}

static CursesFormItem_t mailboxFormItems[] = {
    { FIELD_LABEL, 0, 0, 0, 0, "Mailbox Number: %d", 
      OFFSETOF(mailboxId,Mailbox_t), FA_INTEGER, 0, FT_NONE, { 0 }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 1, 0, 0, "Server:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 1, 32, 1, "%s", OFFSETOF(server,Mailbox_t), FA_STRING,
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 2, 0, 0, "Port:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 2, 6, 1, "%d", OFFSETOF(port,Mailbox_t), FA_INTEGER, 6,
      FT_INTEGER, { .integerArgs = { 0, 0, 65535 } }, NULL, NULL },
    { FIELD_LABEL, 0, 3, 0, 0, "User:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 3, 32, 1, "%s", OFFSETOF(user,Mailbox_t), FA_STRING, 64,
      FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 4, 0, 0, "Password:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 4, 32, 1, "%s", OFFSETOF(password,Mailbox_t), FA_STRING,
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 5, 0, 0, "Protocol:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 5, 32, 1, "%s", OFFSETOF(protocol,Mailbox_t), FA_STRING,
      32, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 6, 0, 0, "Options:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_FIELD, 16, 6, 32, 1, "%s", OFFSETOF(options,Mailbox_t), FA_STRING,
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 7, 0, 0, "Mailbox:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 7, 32, 1, "%s", OFFSETOF(mailbox,Mailbox_t), FA_STRING,
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 8, 0, 0, "Poll Interval:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 8, 20, 1, "%d", OFFSETOF(interval,Mailbox_t), FA_INTEGER,
      20, FT_INTEGER, { .integerArgs = { 0, 60, 86400 } }, NULL, NULL },
    { FIELD_LABEL, 0, 9, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_CHECKBOX, 16, 9, 0, 0, "[%c]", OFFSETOF(enabled,Mailbox_t), FA_BOOL,
      0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 10, 0, 0, "Last Checked:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_LABEL, 16, 10, 0, 0, "%s", OFFSETOF(lastCheck,Mailbox_t), 
      FA_TIMESTAMP, 0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 11, 0, 0, "Last Read:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_LABEL, 16, 11, 0, 0, "%s", OFFSETOF(lastRead,Mailbox_t), 
      FA_TIMESTAMP, 0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 12, 0, 0, "Next Poll:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_LABEL, 16, 12, 0, 0, "%s", OFFSETOF(nextPoll,Mailbox_t), 
      FA_TIMESTAMP, 0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 2, 13, 0, 0, "Revert", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesMailboxRevert, (void *)(-1) },
    { FIELD_BUTTON, 10, 13, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 16, 13, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesCancel, NULL }
};
static int mailboxFormItemCount = NELEMENTS(mailboxFormItems);


void mailboxSaveFunc( void *arg, int index, char *string )
{
    Mailbox_t          *mailbox;

    mailbox = (Mailbox_t *)arg;

    if( index == -1 ) {
        db_update_mailbox( mailbox );
        mailboxSighup( 0, NULL );
        return;
    }

    cursesSaveOffset( arg, index, mailboxFormItems, mailboxFormItemCount,
                      string );
    mailbox->modified = TRUE;
}

void cursesMailboxRevert( void *arg, char *string )
{
    cursesFormRevert( arg, mailboxFormItems, mailboxFormItemCount, 
                      mailboxSaveFunc );
}


void cursesMailboxDisplay( void *arg )
{
    cursesFormDisplay( arg, mailboxFormItems, mailboxFormItemCount, 
                       mailboxSaveFunc );
}


static CursesFormItem_t mailboxReportFormItems[] = {
    { FIELD_LABEL, 0, 0, 0, 0, "Mailbox Number:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 0, 20, 1, "%d", OFFSETOF(mailboxId,MailboxReport_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 1, 4000 } }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 1, 0, 0, "Channel Number:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 1, 20, 1, "%d", OFFSETOF(chanServId,MailboxReport_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 1, 4000 } }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 2, 0, 0, "Nick:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 2, 32, 1, "%s", OFFSETOF(nick,MailboxReport_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 3, 0, 0, "Format:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 3, 32, 1, "%s", OFFSETOF(format,MailboxReport_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 4, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_CHECKBOX, 16, 4, 0, 0, "[%c]", OFFSETOF(enabled,MailboxReport_t), 
      FA_BOOL, 0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 2, 5, 0, 0, "Revert", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesMailboxReportRevert, (void *)(-1) },
    { FIELD_BUTTON, 10, 5, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 16, 5, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesCancel, NULL },
    { FIELD_LABEL, 0, 7, 0, 0, "NOTE: if Nick is set, Channel Number", -1, 
      FA_NONE, 0, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 6, 8, 0, 0, "above is actually Server Number", -1, FA_NONE,
      0, FT_NONE, { 0 }, NULL, NULL }
};
static int mailboxReportFormItemCount = NELEMENTS(mailboxReportFormItems);


void mailboxReportSaveFunc( void *arg, int index, char *string )
{
    MailboxReport_t        *report;

    report = (MailboxReport_t *)arg;

    if( index == -1 ) {
        if( report->mailboxId != report->oldMailboxId ||
            report->chanServId != report->oldChanServId ||
            strcmp( report->nick, report->oldNick ) ) {
            /* 
             * As the report is about to be blown away and reloaded to a new
             * report item, leave the form 
             */
            cursesCancel( arg, string );
        }

        if( strcmp( report->nick, "" ) ) {
            report->channelId = 0;
            report->serverId  = report->chanServId;
        } else {
            report->channelId = report->chanServId;
            report->serverId  = FindServerWithChannel( report->channelId );
        }
        
        db_update_report( report );
        mailboxSighup( 0, NULL );
        return;
    }

    cursesSaveOffset( arg, index, mailboxReportFormItems, 
                      mailboxReportFormItemCount, string );
    report->modified = TRUE;
}

void cursesMailboxReportRevert( void *arg, char *string )
{
    MailboxReport_t        *report;

    report = (MailboxReport_t *)arg;
    report->oldMailboxId = report->mailboxId;
    report->oldChanServId = report->chanServId;
    if( report->oldNick ) {
        free( report->oldNick );
    }
    report->oldNick = strdup( report->nick );

    cursesFormRevert( arg, mailboxReportFormItems, mailboxReportFormItemCount, 
                      mailboxReportSaveFunc );
}


void cursesMailboxReportDisplay( void *arg )
{
    MailboxReport_t        *report;

    report = (MailboxReport_t *)arg;
    report->oldMailboxId = report->mailboxId;
    report->oldChanServId = report->chanServId;
    if( report->oldNick ) {
        free( report->oldNick );
    }
    report->oldNick = strdup( report->nick );

    cursesFormDisplay( arg, mailboxReportFormItems, mailboxReportFormItemCount, 
                       mailboxReportSaveFunc );
}

void mailboxDisableServer( IRCServer_t *server )
{
    bool            changed;

    BalancedBTreeLock( mailboxTree );
    changed = mailboxRecurseDisableServer( mailboxTree->root, server );
    BalancedBTreeUnlock( mailboxTree );

    if( changed ) {
        /* kick the thread */
        pthread_mutex_lock( &signalMutex );
        pthread_cond_broadcast( &kickCond );
        pthread_mutex_unlock( &signalMutex );
    }
}

bool mailboxRecurseDisableServer( BalancedBTreeItem_t *node, 
                                  IRCServer_t *server )
{
    Mailbox_t          *mailbox;
    LinkedListItem_t   *item;
    MailboxReport_t    *report;
    bool                changed;

    if( !node ) {
        return( FALSE );
    }

    changed = mailboxRecurseDisableServer( node->left, server );

    mailbox = (Mailbox_t *)node->item;
    if( mailbox->reports ) {
        LinkedListLock( mailbox->reports );
        for( item = mailbox->reports->head; item; item = item->next ) {
            report = (MailboxReport_t *)item;
            if( report->server == server ) {
                LogPrint( LOG_INFO, "Mailbox: %d: Disabling server", 
                          mailbox->mailboxId );
                report->server = NULL;
                changed = TRUE;
            }
        }
        LinkedListUnlock( mailbox->reports );
    }

    changed = mailboxRecurseDisableServer( node->right, server ) || changed;

    return( changed );
}

void mailboxDisableChannel( IRCChannel_t *channel )
{
    bool            changed;

    BalancedBTreeLock( mailboxTree );
    changed = mailboxRecurseDisableChannel( mailboxTree->root, channel );
    BalancedBTreeUnlock( mailboxTree );

    if( changed ) {
        /* kick the thread */
        pthread_mutex_lock( &signalMutex );
        pthread_cond_broadcast( &kickCond );
        pthread_mutex_unlock( &signalMutex );
    }
}

bool mailboxRecurseDisableChannel( BalancedBTreeItem_t *node, 
                                   IRCChannel_t *channel )
{
    Mailbox_t          *mailbox;
    LinkedListItem_t   *item;
    MailboxReport_t    *report;
    bool                changed;

    if( !node ) {
        return( FALSE );
    }

    changed = mailboxRecurseDisableChannel( node->left, channel );

    mailbox = (Mailbox_t *)node->item;
    if( mailbox->reports ) {
        LinkedListLock( mailbox->reports );
        for( item = mailbox->reports->head; item; item = item->next ) {
            report = (MailboxReport_t *)item;
            if( report->channel == channel ) {
                LogPrint( LOG_INFO, "Mailbox: %d: Disabling channel", 
                          mailbox->mailboxId );
                report->channel = NULL;
                changed = TRUE;
            }
        }
        LinkedListUnlock( mailbox->reports );
    }

    changed = mailboxRecurseDisableChannel( node->right, channel ) || changed;

    return( changed );
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
