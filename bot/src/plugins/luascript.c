/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2007 Gavin Hurlbut
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
* Copyright 2007 Gavin Hurlbut
* All rights reserved
*/

/* INCLUDE FILES */
#define _BSD_SOURCE
#include "environment.h"
#include "botnet.h"
#include <pthread.h>
#include "structs.h"
#include "protos.h"
#include "queue.h"
#include "balanced_btree.h"
#include "linked_list.h"
#include "logging.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

#ifndef PLUGIN_PATH
#define PLUGIN_PATH "./plugins"
#endif


/* INTERNAL FUNCTION PROTOTYPES */
void botCmdLuascript( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                      char *msg );
char *botHelpLuascript( void );

BalancedBTree_t *db_get_luascripts( void );
void result_get_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg );

typedef struct {
    char           *name;
    char           *fileName;
    int             preload;
    int             loaded;
    char           *args;
    lua_State      *L;
    LinkedList_t   *regexps;
    LinkedList_t   *commands;
} Luascript_t;

bool luascriptUnload( char *name );
bool luascriptLoad( char *name );
bool luascriptLoadItem( Luascript_t *luascript );
void luascriptUnloadItem( Luascript_t *luascript );
void luascriptInitializeTree( BalancedBTreeItem_t *item );

static int db_upgrade_schema( int current, int goal );

static int lua_LogPrint( lua_State *L );
static int lua_transmitMsg( lua_State *L );
static int lua_LoggedChannelMessage( lua_State *L );
static int lua_LoggedActionMessage( lua_State *L );
static int lua_regexp_add( lua_State *L );
static int lua_regexp_remove( lua_State *L );
static int lua_botCmd_add( lua_State *L );
static int lua_botCmd_remove( lua_State *L );
int luaopen_mylib( lua_State *L );

static const struct luaL_reg mylib [] = {
    {"LogPrint", lua_LogPrint},
    {"transmitMsg", lua_transmitMsg},
    {"LoggedChannelMessage", lua_LoggedChannelMessage},
    {"LoggedActionMessage", lua_LoggedActionMessage},
    {"regexp_add", lua_regexp_add},
    {"regexp_remove", lua_regexp_remove},
    {"botCmd_add", lua_botCmd_add},
    {"botCmd_remove", lua_botCmd_remove},
    {NULL, NULL}
};

BalancedBTree_t *luascriptTree = NULL;

#define CURRENT_SCHEMA_LUASCRIPT 1
#define MAX_SCHEMA_QUERY 100
typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_luascript` (\n"
    "    `scriptName` VARCHAR( 64 ) NOT NULL ,\n"
    "    `fileName` VARCHAR( 64 ) NOT NULL ,\n"
    "    `preload` INT NOT NULL ,\n"
    "    `arguments` VARCHAR( 255 ) NOT NULL ,\n"
    "    PRIMARY KEY ( `scriptName` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_LUASCRIPT] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t luascriptQueryTable[] = {
    /* 0 */
    { "SELECT scriptName, fileName, preload, arguments FROM plugin_luascript",
      NULL, NULL, FALSE }
};


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char            *command = "luascript";
    char                   *verString;
    int                     ver;
    int                     printed;

    LogPrintNoArg( LOG_NOTICE, "Initializing luascript..." );
    LogPrint( LOG_NOTICE, "Using %s", LUA_VERSION );
    LogPrint( LOG_NOTICE, "%s", LUA_COPYRIGHT );
    LogPrint( LOG_NOTICE, "Script path: %s", PLUGIN_PATH );

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting("dbSchemaLuascript");
        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current LUAscript database schema version %d", 
                                ver );
            LogPrint( LOG_CRIT, "Code supports version %d", 
                                CURRENT_SCHEMA_LUASCRIPT );
            printed = TRUE;
        }

        if( ver < CURRENT_SCHEMA_LUASCRIPT ) {
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA_LUASCRIPT );
        }
    } while( ver < CURRENT_SCHEMA_LUASCRIPT );

    luascriptTree = db_get_luascripts();
    if( !luascriptTree ) {
        LogPrintNoArg( LOG_NOTICE, "No LUA scripts defined, unloading "
                                   "luascript..." );
        return;
    }

    BalancedBTreeLock( luascriptTree );
    if( !luascriptTree->root ) {
        LogPrintNoArg( LOG_NOTICE, "No LUA scripts defined, unloading "
                                   "luascript..." );
        BalancedBTreeUnlock( luascriptTree );
        return;
    }

    luascriptInitializeTree( luascriptTree->root );
    BalancedBTreeUnlock( luascriptTree );

    botCmd_add( (const char **)&command, botCmdLuascript, botHelpLuascript );
}

void plugin_shutdown( void )
{
    BalancedBTreeItem_t    *item;
    Luascript_t            *luascript;

    LogPrintNoArg( LOG_NOTICE, "Removing luascript..." );
    botCmd_remove( "luascript" );

    if( !luascriptTree ) {
        return;
    }
    BalancedBTreeLock( luascriptTree );
    while( (item = luascriptTree->root) ) {
        luascript = (Luascript_t *)item->item;

        if( luascript->loaded ) {
            luascriptUnloadItem( luascript );
        }
        BalancedBTreeRemove( luascriptTree, item, LOCKED, FALSE );

        LinkedListLock( luascript->regexps );
        LinkedListDestroy( luascript->regexps );
        LinkedListLock( luascript->commands );
        LinkedListDestroy( luascript->commands );
        free( luascript );
        free( item );
    }
    BalancedBTreeDestroy( luascriptTree );
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
        LogPrint( LOG_ERR, "Initializing LUAscript database to schema version "
                           "%d", CURRENT_SCHEMA_LUASCRIPT );
        for( i = 0; i < defSchemaCount; i++ ) {
            db_queue_query( i, defSchema, NULL, 0, NULL, NULL, NULL );
        }
        db_set_setting("dbSchemaLuascript", "%d", CURRENT_SCHEMA_LUASCRIPT);
        return( CURRENT_SCHEMA_LUASCRIPT );
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


BalancedBTree_t *db_get_luascripts( void )
{
    BalancedBTree_t    *tree;
    pthread_mutex_t    *mutex;

    tree = BalancedBTreeCreate( BTREE_KEY_STRING );
    if( !tree ) {
        return( tree );
    }
    
    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, luascriptQueryTable, NULL, 0, result_get_luascripts, 
                    tree, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( tree );
}

void result_get_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg )
{
    Luascript_t            *luascript;
    int                     count;
    int                     i;
    MYSQL_ROW               row;
    BalancedBTree_t        *tree;
    BalancedBTreeItem_t    *item;

    tree = (BalancedBTree_t *)arg;

    if( !res || !(count = mysql_num_rows(res)) ) {
        return;
    }

    BalancedBTreeLock( tree );

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        luascript = (Luascript_t *)malloc(sizeof(Luascript_t));
        if( !luascript ) {
            continue;
        }

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( !item ) {
            free( luascript );
            continue;
        }

        memset( luascript, 0, sizeof(Luascript_t) );

        luascript->name     = strdup(row[0]);
        luascript->fileName = strdup(row[1]);
        luascript->preload  = atoi(row[2]);
        luascript->args     = strdup(row[3]);

        luascript->regexps  = LinkedListCreate();
        luascript->commands = LinkedListCreate();

        item->item = luascript;
        item->key  = (void *)&luascript->name;

        BalancedBTreeAdd( tree, item, LOCKED, FALSE );
    }

    /* Rebalance the tree */
    BalancedBTreeAdd( tree, NULL, LOCKED, TRUE );

    BalancedBTreeUnlock( tree );
}


void luascriptInitializeTree( BalancedBTreeItem_t *item )
{
    Luascript_t   *luascript;

    if( !item ) {
        return;
    }

    luascriptInitializeTree( item->left );

    luascript = (Luascript_t *)item->item;
    if( luascript->preload ) {
        luascriptLoadItem( luascript );
    }

    luascriptInitializeTree( item->right );
}

bool luascriptLoad( char *name )
{
    BalancedBTreeItem_t    *item;
    Luascript_t            *luascript;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( luascriptTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    luascript = (Luascript_t *)item->item;

    if( luascript->loaded ) {
        return( false );
    }

    return( luascriptLoadItem( luascript ) );
}

bool luascriptUnload( char *name )
{
    BalancedBTreeItem_t    *item;
    Luascript_t            *luascript;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( luascriptTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    luascript = (Luascript_t *)item->item;

    if( !luascript->loaded ) {
        return( false );
    }

    luascriptUnloadItem( luascript );
    return( true );
}

bool luascriptLoadItem( Luascript_t *luascript )
{
    char                   *scriptfile;
    int                     retval;
    lua_State              *L;

    scriptfile = (char *)malloc(strlen(PLUGIN_PATH) + 
                         strlen(luascript->fileName) + 3 );
    sprintf( scriptfile, "%s/%s", PLUGIN_PATH, luascript->fileName );

    LogPrint( LOG_NOTICE, "Loading LUA script %s from %s", luascript->name, 
                          scriptfile );

    L = lua_open();
    luascript->L = L;
    luaopen_base(L);
    luaopen_table(L);
    luaopen_io(L);
    luaopen_string(L);
    luaopen_math(L);
    luaopen_mylib(L);

    retval = luaL_loadfile(L, scriptfile);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error loading LUA script %s: %s", luascript->name,
                            lua_tostring(L, -1) );
        return( FALSE );
    }
    free( scriptfile );

    retval = lua_pcall(L, 0, 0, 0);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error starting LUA script %s: %s", luascript->name,
                            lua_tostring(L, -1) );
        return( FALSE );
    }

    lua_getglobal(luascript->L, "initialize");
    retval = lua_pcall(L, 0, 0, 0);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error starting LUA script %s: %s", luascript->name,
                            lua_tostring(L, -1) );
        return( FALSE );
    }

    luascript->loaded = true;
    return( TRUE );
}

void luascriptUnloadItem( Luascript_t *luascript )
{
    if( !luascript ) {
        return;
    }

    lua_getglobal(luascript->L, "shutdown");
    lua_pcall(luascript->L, 0, 0, 0);

    LogPrint( LOG_NOTICE, "Unloading LUA script %s", luascript->name );
    lua_close(luascript->L);
    luascript->loaded = false;
}


void botCmdLuascript( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                      char *msg )
{
    int             len;
    char           *line;
    char           *command;
    char           *message;
    bool            ret;
    static char    *notauth = "You are not authorized, you can't do that!";

    if( !server || channel ) {
        return;
    }

    if( !authenticate_check( server, who ) ) {
        transmitMsg( server, TX_PRIVMSG, who, notauth );
        return;
    }

    line = strstr( msg, " " );
    if( line ) {
        /* Command has trailing text, skip the space */
        len = line - msg;
        line++;

        command = (char *)malloc( len + 2 );
        strncpy( command, msg, len );
        command[len] = '\0';
    } else {
        /* Command is the whole line */
        command = strdup( msg );
    }

    /* Strip trailing spaces */
    if( line ) {
        for( len = strlen(line); len && line[len-1] == ' ';
             len = strlen(line) ) {
            line[len-1] = '\0';
        }

        if( *line == '\0' ) {
            line = NULL;
        }
    }

    if( !strcmp( command, "list" ) ) {
        BalancedBTreeLock( luascriptTree );
        if( line && !strcmp( line, "all" ) ) {
            message = botCmdDepthFirst( luascriptTree->root, false );
        } else {
            message = botCmdDepthFirst( luascriptTree->root, true );
        }
        BalancedBTreeUnlock( luascriptTree );
    } else if( !strcmp( command, "load" ) && line ) {
        ret = luascriptLoad( line );
        message = (char *)malloc(strlen(line) + 36);
        if( ret ) {
            sprintf( message, "Loaded LUA script %s", line );
        } else {
            sprintf( message, "LUA script %s already loaded", line );
        }
    } else if( !strcmp( command, "unload" ) && line ) {
        ret = luascriptUnload( line );
        message = (char *)malloc(strlen(line) + 36);
        if( ret ) {
            sprintf( message, "Unloaded LUA script %s", line );
        } else {
            sprintf( message, "LUA script %s already unloaded", line );
        }
    } else {
        message = NULL;
        free( command );
        return;
    }

    transmitMsg( server, TX_MESSAGE, who, message );

    free( message );
    free( command );
}

char *botHelpLuascript( void )
{
    static char *help = "Loads, unloads and lists LUA scripted plugins "
                        "(requires authentication)  "
                        "Syntax: (in privmsg) luascript load script/"
                        "unload script/list";
    
    return( help );
}

static int lua_LogPrint( lua_State *L )
{
    char           *message;

    message = (char *)luaL_checkstring(L, 1);
    LogPrint( LOG_NOTICE, "%s", message );
    return( 0 );
}

static int lua_transmitMsg( lua_State *L )
{
    IRCServer_t    *server;
    TxType_t        type;
    char           *who, *message;

    server = (IRCServer_t *)luaL_checkudata(L, 1, "IRCServer_t");
    type = (TxType_t)luaL_checkint(L, 2);
    who = (char *)luaL_checkstring(L, 3);
    message = (char *)luaL_checkstring(L, 4);

    if( server && who && message ) {
        transmitMsg( server, type, who, message );
    }
    return( 0 );
}

static int lua_LoggedChannelMessage( lua_State *L )
{
    IRCServer_t    *server;
    IRCChannel_t   *channel;
    char           *message;

    server = (IRCServer_t *)luaL_checkudata(L, 1, "IRCServer_t");
    channel = (IRCChannel_t *)luaL_checkudata(L, 2, "IRCChannel_t");
    message = (char *)luaL_checkstring(L, 3);

    if( server && channel && message ) {
        LoggedChannelMessage( server, channel, message );
    }

    return( 0 );
}

static int lua_LoggedActionMessage( lua_State *L )
{
    IRCServer_t    *server;
    IRCChannel_t   *channel;
    char           *message;

    server = (IRCServer_t *)luaL_checkudata(L, 1, "IRCServer_t");
    channel = (IRCChannel_t *)luaL_checkudata(L, 2, "IRCChannel_t");
    message = (char *)luaL_checkstring(L, 3);

    if( server && channel && message ) {
        LoggedActionMessage( server, channel, message );
    }

    return( 0 );
}

static int lua_regexp_add( lua_State *L )
{
    return( 0 );
}

static int lua_regexp_remove( lua_State *L )
{
    return( 0 );
}

static int lua_botCmd_add( lua_State *L )
{
    return( 0 );
}

static int lua_botCmd_remove( lua_State *L )
{
    return( 0 );
}

int luaopen_mylib( lua_State *L )
{
    luaL_openlib(L, "beirdobot", mylib, 0);
    return( 1 );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
