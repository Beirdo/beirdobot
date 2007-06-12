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

#define CURRENT_SCHEMA_MAILBOX  1
#define MAX_SCHEMA_QUERY 100

typedef struct {
    int         mailboxId;
    char       *server;
    char       *user;
    char       *password;
    char       *protocol;
    char       *options;
    int         interval;
    int         lastCheck;
    int         lastRead;
    time_t      nextPoll;
    bool        enabled;
} Mailbox_t;

typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_mailbox` (\n"
    "    `mailboxId` INT NULL AUTO_INCREMENT PRIMARY KEY ,\n"
    "    `server` VARCHAR( 255 ) NOT NULL ,\n"
    "    `user` VARCHAR( 255 ) NOT NULL ,\n"
    "    `password` VARCHAR( 255 ) NOT NULL ,\n"
    "    `protocol` VARCHAR( 32 ) NOT NULL ,\n"
    "    `options` VARCHAR( 255 ) NOT NULL ,\n"
    "    `pollInterval` INT NOT NULL DEFAULT '600',\n"
    "    `lastCheck` INT NOT NULL DEFAULT '0',\n"
    "    `lastRead` INT NOT NULL DEFAULT '0'\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_MAILBOX] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t mailboxQueryTable[] = {
    /* 0 */
    { "SELECT mailboxId, server, user, password, protocol, options, "
      "pollInterval, lastCheck, lastRead FROM `plugin_mailbox` ORDER BY "
      "`mailboxId` ASC", NULL, NULL, FALSE },
    /* 1 */
    { "UPDATE `plugin_mailbox` SET `lastCheck` = ?, `lastRead` = ? "
      "WHERE `mailboxId` = ?", NULL,
      NULL, FALSE }
};


/* INTERNAL FUNCTION PROTOTYPES */
void botCmdMailbox( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, void *tag );
char *botHelpMailbox( void *tag );
void *mailbox_thread(void *arg);
static int db_upgrade_schema( int current, int goal );
static void db_load_mailboxes( void );
static void result_load_mailboxes( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args );
void mailboxFindUnconflictingTime( BalancedBTree_t *tree, time_t *key );
char *botMailboxDump( BalancedBTreeItem_t *item );


/* INTERNAL VARIABLES  */
pthread_t               mailboxThreadId;
static bool             threadAbort = FALSE;
static pthread_mutex_t  shutdownMutex;
static pthread_mutex_t  signalMutex;
static pthread_cond_t   kickCond;
BalancedBTree_t        *mailboxTree;
BalancedBTree_t        *mailboxActiveTree;


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

    db_load_mailboxes();

    pthread_mutex_init( &shutdownMutex, NULL );
    pthread_mutex_init( &signalMutex, NULL );
    pthread_cond_init( &kickCond, NULL );

    thread_create( &mailboxThreadId, mailbox_thread, NULL, "mailbox_rssfeed" );
    botCmd_add( (const char **)&command, botCmdMailbox, botHelpMailbox, NULL );
}

void plugin_shutdown( void )
{
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

    thread_deregister( mailboxThreadId );
}

void *mailbox_thread(void *arg)
{

    pthread_mutex_lock( &shutdownMutex );
    db_thread_init();

    LogPrintNoArg( LOG_NOTICE, "Starting Mailbox thread" );

    sleep(5);

    while( !GlobalAbort && !threadAbort ) {
        sleep(10);
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

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, mailboxQueryTable, NULL, 0, result_load_mailboxes,
                    NULL, mutex );
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );
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

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    gettimeofday( &tv, NULL );
    nextpoll = tv.tv_sec;

    BalancedBTreeLock( mailboxTree );
    BalancedBTreeLock( mailboxActiveTree );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        mailbox = (Mailbox_t *)malloc(sizeof(Mailbox_t));
        memset( mailbox, 0x00, sizeof(Mailbox_t) );

        mailbox->mailboxId = atoi(row[0]);
        mailbox->server    = strdup(row[1]);
        mailbox->user      = strdup(row[2]);
        mailbox->password  = strdup(row[3]);
        mailbox->protocol  = strdup(row[4]);
        mailbox->options   = strdup(row[5]);
        mailbox->interval  = atoi(row[6]);
        mailbox->lastCheck = atol(row[7]);
        mailbox->lastRead  = atol(row[8]);
        if( mailbox->lastCheck + mailbox->interval <= nextpoll + 1 ) {
            mailbox->nextPoll  = nextpoll++;
        } else {
            mailbox->nextPoll = mailbox->lastCheck + mailbox->interval;
        }
        mailbox->enabled   = TRUE;

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)mailbox;
        item->key  = (void *)&mailbox->mailboxId;
        BalancedBTreeAdd( mailboxTree, item, LOCKED, FALSE );

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)mailbox;
        item->key  = (void *)&mailbox->nextPoll;

        /* Adjust the poll time to avoid conflict */
        mailboxFindUnconflictingTime( mailboxActiveTree, &mailbox->nextPoll );
        BalancedBTreeAdd( mailboxActiveTree, item, LOCKED, FALSE );
        LogPrint( LOG_NOTICE, "Mailbox: Loaded %d: server %s, user %s, "
                              "interval %d", mailbox->mailboxId,
                              mailbox->server, mailbox->user, 
                              mailbox->interval );
    }

    BalancedBTreeAdd( mailboxTree, NULL, LOCKED, TRUE );
    BalancedBTreeAdd( mailboxActiveTree, NULL, LOCKED, TRUE );

    message = botMailboxDump( mailboxActiveTree->root );
    LogPrint( LOG_NOTICE, "Mailbox: %s", message );
    free( message );

    BalancedBTreeUnlock( mailboxTree );
    BalancedBTreeUnlock( mailboxActiveTree );
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
    sprintf( buf, "%d %s@%s(%ld/%ld)", mailbox->mailboxId, mailbox->user, 
                  mailbox->server, mailbox->nextPoll,
                  mailbox->nextPoll - now.tv_sec );

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

/*
 * Callbacks for the UW c-client library
 */

void mm_flags( MAILSTREAM *stream, unsigned long number )
{
}

void mm_status( MAILSTREAM *stream, char *mailbox, MAILSTATUS *status )
{
}

void mm_searched( MAILSTREAM *stream, unsigned long number )
{
}

void mm_exists( MAILSTREAM *stream, unsigned long number )
{
}

void mm_expunged( MAILSTREAM *stream, unsigned long number )
{
}

void mm_list( MAILSTREAM *stream, int delimiter, char *name, long attributes )
{
}

void mm_lsub( MAILSTREAM *stream, int delimiter, char *name, long attributes )
{
}

void mm_notify( MAILSTREAM *stream, char *string, long errflg )
{
}

void mm_log( char *string, long errflg )
{
}

void mm_dlog( char *string )
{
}

void mm_login( NETMBX *mb, char *user, char *pwd, long trial )
{
}

void mm_critical( MAILSTREAM *stream )
{
}

void mm_nocritical( MAILSTREAM *stream )
{
}

long mm_diskerror( MAILSTREAM *stream, long errcode, long serious )
{
    return( 0 );
}

void mm_fatal( char *string )
{
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
