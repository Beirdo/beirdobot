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

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_MAILBOX] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t mailboxQueryTable[] = {
    /* 0 */
    { "SELECT a.`feedid`, a.`chanid`, b.`serverid`, a.`url`, a.`prefix`, "
      "a.`timeout`, a.`lastpost`, a.`feedoffset` FROM `plugin_rssfeed` AS a, "
      "`channels` AS b WHERE a.`chanid` = b.`chanid` ORDER BY a.`feedid` ASC",
      NULL, NULL, FALSE },
    /* 1 */
    { "UPDATE `plugin_rssfeed` SET `lastpost` = ? WHERE `feedid` = ?", NULL,
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


/* INTERNAL VARIABLES  */
pthread_t               mailboxThreadId;
static bool             threadAbort = FALSE;
static pthread_mutex_t  shutdownMutex;
static pthread_mutex_t  signalMutex;
static pthread_cond_t   kickCond;


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
    (void)mailboxQueryTable;
    (void)result_load_mailboxes;
}

static void result_load_mailboxes( MYSQL_RES *res, MYSQL_BIND *input, 
                                   void *args )
{
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
