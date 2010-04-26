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
                      char *msg, void *tag );
char *botHelpLuascript( void *tag );

BalancedBTree_t *db_get_luascripts( void );
void db_check_luascripts( LinkedList_t *list );
void result_check_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                              long insertid );
void result_get_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                            long insertid );
void cursesLuascriptDisplay( void *arg );
void luascriptSaveFunc( void *arg, int index, char *string );

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
int              luascriptMenuId;

#define CURRENT_SCHEMA_LUASCRIPT 1

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
      NULL, NULL, FALSE },
    /* 1 */
    { "SELECT `scriptName` FROM `plugin_luascript` WHERE `scriptName` = ?", 
      NULL, NULL, FALSE },
    /* 2 */
    { "INSERT INTO `plugin_luascript` ( `scriptName`, `fileName`, `preload`, "
      "`arguments`) VALUES ( ?, ?, ?, ? )", NULL, NULL, FALSE }
};

typedef struct {
    LinkedListItem_t    link;
    const char         *channelRegexp;
    const char         *contentRegexp;
    const char         *callback;
    lua_State          *L;
} LuaRegexp_t;

typedef struct {
    LinkedListItem_t    link;
    const char         *command;
    const char         *commandCallback;
    const char         *helpCallback;
    lua_State          *L;
} LuaBotCmd_t;

static void luaTrashRegexp( Luascript_t *luascript, LuaRegexp_t *regexp );
static void luaTrashCommand( Luascript_t *luascript, LuaBotCmd_t *cmd );
void luaRegexpFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag );
void luaCommandFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *msg, void *tag );
char *luaHelpFunc( void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char            *command = "luascript";
    LinkedList_t           *list;

    LogPrintNoArg( LOG_NOTICE, "Initializing luascript..." );
    LogPrint( LOG_NOTICE, "Using %s", LUA_VERSION );
    versionAdd( "LUA", LUA_VERSION );
    LogPrint( LOG_NOTICE, "%s", LUA_COPYRIGHT );
    LogPrint( LOG_NOTICE, "Script path: %s", PLUGIN_PATH );

    db_check_schema( "dbSchemaLuascript", "LUAscript", 
                     CURRENT_SCHEMA_LUASCRIPT, defSchema, defSchemaCount,
                     schemaUpgrade );

    luascriptMenuId = cursesMenuItemAdd( 1, -1, "LUAScript", NULL, NULL );

    list = pluginFindPlugins( NULL, ".lua" );
    db_check_luascripts( list );
    LinkedListDestroy( list );

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

    botCmd_add( (const char **)&command, botCmdLuascript, botHelpLuascript,
                NULL );
}

void plugin_shutdown( void )
{
    BalancedBTreeItem_t    *item;
    Luascript_t            *luascript;

    LogPrintNoArg( LOG_NOTICE, "Removing luascript..." );
    botCmd_remove( "luascript" );

    versionRemove( "LUA" );

    cursesMenuItemRemove( 1, luascriptMenuId, "LUAScript" );
    if( !luascriptTree ) {
        return;
    }
    BalancedBTreeLock( luascriptTree );
    while( (item = luascriptTree->root) ) {
        luascript = (Luascript_t *)item->item;

        cursesMenuItemRemove( 2, luascriptMenuId, luascript->name );
        if( luascript->loaded ) {
            luascriptUnloadItem( luascript );
        }
        BalancedBTreeRemove( luascriptTree, item, LOCKED, FALSE );

        free( luascript->name );
        free( luascript->fileName );
        free( luascript->args );

        LinkedListLock( luascript->regexps );
        LinkedListDestroy( luascript->regexps );
        LinkedListLock( luascript->commands );
        LinkedListDestroy( luascript->commands );
        free( luascript );
        free( item );
    }
    BalancedBTreeDestroy( luascriptTree );
}

void db_check_luascripts( LinkedList_t *list )
{
    MYSQL_BIND         *data;
    pthread_mutex_t    *mutex;
    int                 found;
    LinkedListItem_t   *item, *next;
    PluginItem_t       *plugItem;

    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    LinkedListLock( list );
    for( item = list->head; item; item = next ) {
        next = item->next;
        plugItem = (PluginItem_t *)item;

        data = (MYSQL_BIND *)malloc( 1 * sizeof(MYSQL_BIND));
        memset( data, 0, 1 * sizeof(MYSQL_BIND) );

        bind_string( &data[0], plugItem->plugin, MYSQL_TYPE_VAR_STRING );

        db_queue_query( 1, luascriptQueryTable, data, 1, 
                        result_check_luascripts, &found, mutex );
        pthread_mutex_unlock( mutex );

        if( !found ) {
            LogPrint( LOG_NOTICE, "Adding new LUA script to database: %s (%s)",
                                  plugItem->plugin, "disabled" );

            data = (MYSQL_BIND *)malloc( 4 * sizeof(MYSQL_BIND));
            memset( data, 0, 4 * sizeof(MYSQL_BIND) );

            bind_string( &data[0], plugItem->plugin, MYSQL_TYPE_VAR_STRING );
            bind_string( &data[1], plugItem->script, MYSQL_TYPE_VAR_STRING );
            bind_numeric( &data[2], 0, MYSQL_TYPE_LONG );
            bind_string( &data[3], "", MYSQL_TYPE_VAR_STRING );

            db_queue_query( 2, luascriptQueryTable, data, 4, NULL, NULL, mutex);
            pthread_mutex_unlock( mutex );
        }

        free( plugItem->plugin );
        free( plugItem->script );
        LinkedListRemove( list, item, LOCKED );
        free( item );
    }
    LinkedListUnlock( list );

    pthread_mutex_destroy( mutex );
    free( mutex );
}

void result_check_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                              long insertid )
{
    int         *found;

    found = (int *)arg;

    if( !res || !(mysql_num_rows(res)) ) {
        *found = FALSE;
    } else {
        *found = TRUE;
    }
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

void result_get_luascripts( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                            long insertid )
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

        cursesMenuItemAdd( 2, luascriptMenuId, luascript->name, 
                           cursesLuascriptDisplay, luascript );
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

    lua_pushlightuserdata(L, (void *)luascript);
    lua_setglobal(L, "luascript");

    lua_pushlightuserdata(L, NULL);
    lua_setglobal(L, "null");

    lua_getglobal(L, "initialize");
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
    LinkedListItem_t       *item;
    LuaRegexp_t            *regexp;
    LuaBotCmd_t            *cmd;

    if( !luascript ) {
        return;
    }

    lua_getglobal(luascript->L, "shutdown");
    lua_pcall(luascript->L, 0, 0, 0);

    LogPrint( LOG_NOTICE, "Unloading LUA script %s", luascript->name );
    lua_close(luascript->L);
    luascript->loaded = false;

    LinkedListLock( luascript->regexps );
    for( item = luascript->regexps->head; item; 
         item = luascript->regexps->head ) {
        regexp = (LuaRegexp_t *)item;

        luaTrashRegexp( luascript, regexp );
    }
    LinkedListUnlock( luascript->regexps );

    LinkedListLock( luascript->commands );
    for( item = luascript->commands->head; item; 
         item = luascript->commands->head ) {
        cmd = (LuaBotCmd_t *)item;

        luaTrashCommand( luascript, cmd );
    }
    LinkedListUnlock( luascript->commands );
}


void botCmdLuascript( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                      char *msg, void *tag )
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

char *botHelpLuascript( void *tag )
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

    server = (IRCServer_t *)lua_touserdata(L, 1);
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

    server = (IRCServer_t *)lua_touserdata(L, 1);
    channel = (IRCChannel_t *)lua_touserdata(L, 2);
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

    server = (IRCServer_t *)lua_touserdata(L, 1);
    channel = (IRCChannel_t *)lua_touserdata(L, 2);
    message = (char *)luaL_checkstring(L, 3);

    if( server && channel && message ) {
        LoggedActionMessage( server, channel, message );
    }

    return( 0 );
}

static int lua_regexp_add( lua_State *L )
{
    char           *channelRegexp;
    char           *contentRegexp;
    char           *callback;
    LuaRegexp_t    *item;
    Luascript_t    *luascript;

    channelRegexp = (char *)lua_tostring(L, 1);
    contentRegexp = (char *)lua_tostring(L, 2);
    callback      = (char *)luaL_checkstring(L, 3);

    lua_getglobal(L, "luascript");
    luascript = (Luascript_t *)lua_touserdata(L, -1);

    item = (LuaRegexp_t *)malloc(sizeof(LuaRegexp_t));
    item->channelRegexp = ( channelRegexp ? 
                            (const char *)strdup(channelRegexp) :
                            NULL );
    item->contentRegexp = ( contentRegexp ?
                            (const char *)strdup(contentRegexp) :
                            NULL );
    item->callback      = ( callback ?
                            (const char *)strdup(callback) :
                            NULL );
    item->L = luascript->L;

    LinkedListAdd( luascript->regexps, (LinkedListItem_t *)item, UNLOCKED,
                   AT_TAIL );

    regexp_add( item->channelRegexp, item->contentRegexp, luaRegexpFunc, item );
    return( 0 );
}

static void luaTrashRegexp( Luascript_t *luascript, LuaRegexp_t *regexp )
{
    LinkedListItem_t       *item;

    if( !luascript || !regexp ) {
        return;
    }

    item = (LinkedListItem_t *)regexp;

    regexp_remove( (char *)regexp->channelRegexp, 
                   (char *)regexp->contentRegexp );

    LinkedListRemove( luascript->regexps, item, LOCKED );

    if( regexp->channelRegexp ) {
        free( (char *)regexp->channelRegexp );
    }

    if( regexp->contentRegexp ) {
        free( (char *)regexp->contentRegexp );
    }

    if( regexp->callback ) {
        free( (char *)regexp->callback );
    }

    free( item );
}

static void luaTrashCommand( Luascript_t *luascript, LuaBotCmd_t *cmd )
{
    LinkedListItem_t       *item;

    if( !luascript || !cmd ) {
        return;
    }

    item = (LinkedListItem_t *)cmd;

    botCmd_remove( (char *)cmd->command ); 

    LinkedListRemove( luascript->commands, item, LOCKED );

    if( cmd->command ) {
        free( (char *)cmd->command );
    }

    if( cmd->commandCallback ) {
        free( (char *)cmd->commandCallback );
    }

    if( cmd->helpCallback ) {
        free( (char *)cmd->helpCallback );
    }

    free( item );
}

static int lua_regexp_remove( lua_State *L )
{
    char               *channelRegexp;
    char               *contentRegexp;
    Luascript_t        *luascript;
    LinkedListItem_t   *item;
    LuaRegexp_t        *regexp;
    bool                found;

    channelRegexp = (char *)luaL_checkstring(L, 1);
    contentRegexp = (char *)luaL_checkstring(L, 2);

    lua_getglobal(L, "luascript");
    luascript = (Luascript_t *)lua_touserdata(L, -1);

    if( !luascript ) {
        return( 0 );
    }

    LinkedListLock(luascript->regexps);

    for( item = luascript->regexps->head, found = FALSE; item && !found; 
         item = item->next ) {
        regexp = (LuaRegexp_t *)item;

        if( !strcmp( channelRegexp, regexp->channelRegexp ) &&
            !strcmp( contentRegexp, regexp->contentRegexp ) ) {
            found = TRUE;
        }
    }

    if( found ) {
        luaTrashRegexp( luascript, regexp );
    }

    LinkedListUnlock(luascript->regexps);
    return( 0 );
}

static int lua_botCmd_add( lua_State *L )
{
    char           *command;
    char           *commandCallback;
    char           *helpCallback;
    LuaBotCmd_t    *item;
    Luascript_t    *luascript;

    command         = (char *)lua_tostring(L, 1);
    commandCallback = (char *)lua_tostring(L, 2);
    helpCallback    = (char *)lua_tostring(L, 3);

    lua_getglobal(L, "luascript");
    luascript = (Luascript_t *)lua_touserdata(L, -1);

    item = (LuaBotCmd_t *)malloc(sizeof(LuaBotCmd_t));
    item->command = ( command ? 
                      (const char *)strdup(command) :
                      NULL );
    item->commandCallback = ( commandCallback ?
                              (const char *)strdup(commandCallback) :
                              NULL );
    item->helpCallback    = ( helpCallback ?
                              (const char *)strdup(helpCallback) :
                              NULL );
    item->L = luascript->L;

    LinkedListAdd( luascript->commands, (LinkedListItem_t *)item, UNLOCKED,
                   AT_TAIL );

    botCmd_add( &item->command, luaCommandFunc, luaHelpFunc, item );
    return( 0 );
}

static int lua_botCmd_remove( lua_State *L )
{
    char               *command;
    Luascript_t        *luascript;
    LinkedListItem_t   *item;
    LuaBotCmd_t        *cmd;
    bool                found;

    command = (char *)luaL_checkstring(L, 1);

    lua_getglobal(L, "luascript");
    luascript = (Luascript_t *)lua_touserdata(L, -1);

    if( !luascript ) {
        return( 0 );
    }

    LinkedListLock(luascript->commands);

    for( item = luascript->commands->head, found = FALSE; item && !found; 
         item = item->next ) {
        cmd = (LuaBotCmd_t *)item;

        if( !strcmp( command, cmd->command ) ) {
            found = TRUE;
        }
    }

    if( found ) {
        luaTrashCommand( luascript, cmd );
    }

    LinkedListUnlock(luascript->commands);
    return( 0 );
}

int luaopen_mylib( lua_State *L )
{
    luaL_openlib(L, "beirdobot", mylib, 0);
    return( 1 );
}


void luaRegexpFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                    char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                    void *tag )
{
    lua_State          *L;
    LuaRegexp_t        *regexp;
    int                 retval;

    regexp = (LuaRegexp_t *)tag;
    if( !regexp || !regexp->callback ) {
        return;
    }

    L = regexp->L;

    /* same order to how they appear in the function def in lua */
    lua_getglobal(L, regexp->callback);
    lua_pushlightuserdata(L, (void *)server);
    lua_pushlightuserdata(L, (void *)channel);
    lua_pushstring(L, who);
    lua_pushstring(L, msg);
    lua_pushnumber(L, type);

    retval = lua_pcall(L, 5, 0, 0);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error calling LUA script callback %s : %s", 
                            regexp->callback, lua_tostring(L, -1) );
    }
}

void luaCommandFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *msg, void *tag )
{
    lua_State          *L;
    LuaBotCmd_t        *cmd;
    int                 retval;

    cmd = (LuaBotCmd_t *)tag;
    if( !cmd || !cmd->commandCallback ) {
        return;
    }

    L = cmd->L;

    /* same order to how they appear in the function def in lua */
    lua_getglobal(L, cmd->commandCallback);
    lua_pushlightuserdata(L, (void *)server);
    lua_pushlightuserdata(L, (void *)channel);
    lua_pushstring(L, who);
    lua_pushstring(L, msg);

    retval = lua_pcall(L, 4, 0, 0);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error calling LUA script callback %s : %s", 
                            cmd->commandCallback, lua_tostring(L, -1) );
    }
}

char *luaHelpFunc( void *tag )
{
    lua_State          *L;
    LuaBotCmd_t        *cmd;
    int                 retval;
    char               *msg;

    cmd = (LuaBotCmd_t *)tag;
    if( !cmd || !cmd->helpCallback ) {
        return( NULL );
    }

    L = cmd->L;

    lua_getglobal(L, cmd->helpCallback);

    retval = lua_pcall(L, 0, 1, 0);
    if( retval ) {
        LogPrint( LOG_CRIT, "Error calling LUA script callback %s : %s", 
                            cmd->commandCallback, lua_tostring(L, -1) );
    }

    msg = (char *)lua_tostring(L, -1);

    return( msg );
}


static CursesFormItem_t luascriptFormItems[] = {
    { FIELD_LABEL, 1, 1, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_CHECKBOX, 12, 1, 0, 0, "[%c]", OFFSETOF(preload,Luascript_t), 
      FA_BOOL, 3, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 4, 3, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 9, 3, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesCancel, NULL }
};
static int luascriptFormItemCount = NELEMENTS(luascriptFormItems);

void cursesLuascriptDisplay( void *arg )
{
    cursesFormDisplay( arg, luascriptFormItems, luascriptFormItemCount,
                       luascriptSaveFunc );
}

void luascriptSaveFunc( void *arg, int index, char *string )
{
    Luascript_t    *luascript;

    luascript = (Luascript_t *)arg;

    if( index == -1 ) {
        if( luascript->preload && !luascript->loaded ) {
            luascriptLoadItem( luascript );
        } else if( !luascript->preload && luascript->loaded ) {
            luascriptUnloadItem( luascript );
        }
        return;
    }

    cursesSaveOffset( arg, index, luascriptFormItems, luascriptFormItemCount, 
                      string );
}



/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
