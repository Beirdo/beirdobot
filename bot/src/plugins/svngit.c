/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2010 Gavin Hurlbut
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
* Copyright 2010 Gavin Hurlbut
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
#include <math.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"
#include "logging.h"

#define CURRENT_SCHEMA_URL 1

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_svngit` (\n"
    "    `repo` VARCHAR(64) NOT NULL ,\n"
    "    `branch` VARCHAR(255) NOT NULL ,\n"
    "    `sha1` VARCHAR(40) NOT NULL ,\n"
    "    `svnid` INT NOT NULL ,\n"
    "    PRIMARY KEY ( `svnid`, `repo`, `branch` ) ,\n"
    "    INDEX `git` ( `sha1` , `repo`, `branch` ) \n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_URL] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t svnQueryTable[] = {
    /* 0 */
    { "SELECT `repo`, `branch`, `sha1` FROM `plugin_svngit` WHERE `svnid` = ?",
      NULL, NULL, FALSE }
};

typedef struct {
    char   *repo;
    char   *branch;
    char   *hash;
} GitSha_t;


/* INTERNAL FUNCTION PROTOTYPES */
void botCmdSvn( IRCServer_t *server, IRCChannel_t *channel, char *who,
                char *msg, void *tag );
char *botHelpSvn( void *tag );
void regexpFuncSvn( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag );

char *get_svn_git( char *svnid );
GitSha_t *db_get_svn_git( char *svnid );
void result_get_svn_git( MYSQL_RES *res, MYSQL_BIND *input, void *args, 
                         long insertid );

static char    *svnRegexp = "(?i)(?:\\s|^)\\[(\\d+)\\](?:\\s|$)";

void plugin_initialize( char *args )
{
    static char    *command = "svn";

    LogPrintNoArg( LOG_NOTICE, "Initializing svngit plugin..." );

    db_check_schema( "dbSchemaSvnGit", "svngit", CURRENT_SCHEMA_URL, defSchema,
                     defSchemaCount, schemaUpgrade );

    botCmd_add( (const char **)&command, botCmdSvn, botHelpSvn, NULL );
    regexp_add( NULL, (const char *)svnRegexp, regexpFuncSvn, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing svngit plugin..." );

    regexp_remove( NULL, svnRegexp );
    botCmd_remove( "svn" );
}


void botCmdSvn( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                char *msg, void *tag )
{
    char           *message;
    char           *svnid;

    if( !server || !msg ) {
        return;
    }

    message = NULL;
    svnid = CommandLineParse( msg, &msg );
    if( svnid ) {
        message = get_svn_git( svnid );
    } else {
        message = strdup( "You need to specify the SVN version!" );
    }

    if( message ) {
        if( !channel ) {
            transmitMsg( server, TX_PRIVMSG, who, message);
        } else {
            LoggedChannelMessage(server, channel, message);
        }

        free( message );
    }
}

char *get_svn_git( char *svnid )
{
    GitSha_t   *gitsha;
    GitSha_t   *sha;
    int         len;
    char       *message;

    gitsha = db_get_svn_git( svnid );
    if( !gitsha ) {
        message = (char *)malloc(27 + strlen(svnid));
        sprintf( message, "No match for SVN revision %s", svnid );
    } else {
        message = (char *)malloc(6 + strlen(svnid));
        sprintf( message, "SVN %s:", svnid );
        len = strlen(message);
        for( sha = gitsha; sha->repo; sha++ ) {
            len += strlen(sha->repo) + strlen(sha->branch) + 11;
            message = (char *)realloc( message, len + 1 );
            sha->hash[8] = '\0';
            sprintf( message, "%s %s:%s/%s", message, sha->repo, 
                                             sha->branch, sha->hash );
            free( sha->repo );
            free( sha->branch );
            free( sha->hash );
        }
        free( gitsha );
    }

    return( message );
}

char *botHelpSvn( void *tag )
{
    static char *help = "Returns the git SHA1 information that match the "
                        "requested SVN version.   Syntax: svn revnum ";

    return( help );
}

GitSha_t *db_get_svn_git( char *svnid )
{
    pthread_mutex_t        *mutex;
    MYSQL_BIND             *data;
    GitSha_t               *gitsha;

    if( !svnid ) {
        return( NULL );
    }

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    data = (MYSQL_BIND *)malloc(1 * sizeof(MYSQL_BIND));
    memset( data, 0, 1 * sizeof(MYSQL_BIND) );

    bind_numeric( &data[0], atoi(svnid), MYSQL_TYPE_LONG );

    db_queue_query( 0, svnQueryTable, data, 1, result_get_svn_git,
                    &gitsha, mutex );

    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( gitsha );
}


void result_get_svn_git( MYSQL_RES *res, MYSQL_BIND *input, void *args, 
                         long insertid )
{
    MYSQL_ROW       row;
    GitSha_t      **pGitsha;
    GitSha_t       *gitsha;
    int             count;
    int             i;

    pGitsha = (GitSha_t **)args;

    if( !res || !(count = mysql_num_rows(res)) ) {
        *pGitsha = NULL;
        return;
    }

    gitsha = (GitSha_t *)malloc((count + 1) * sizeof(GitSha_t));
    memset( gitsha, 0x00, (count + 1) * sizeof(GitSha_t) );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        gitsha[i].repo   = strdup(row[0]);
        gitsha[i].branch = strdup(row[1]);
        gitsha[i].hash   = strdup(row[2]);
    }

    *pGitsha = gitsha;
}

void regexpFuncSvn( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag )
{
    char               *string;
    char               *message;

    string = regexp_substring( msg, ovector, ovecsize, 1 );
    message = get_svn_git( string );

    free( string );

    if( message ) {
        if( !channel ) {
            transmitMsg( server, TX_PRIVMSG, who, message);
        } else {
            LoggedChannelMessage(server, channel, message);
        }

        free( message );
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
