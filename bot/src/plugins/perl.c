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
#include <string.h>
#define PERL_GCC_BRACE_GROUPS_FORBIDDEN
#include <EXTERN.h>
#include <perl.h>
#include "embedding.pl.h"

#ifndef PLUGIN_PATH
#define PLUGIN_PATH "./plugins"
#endif

#define PERL_MODULE_PREFIX "BeirdoBot"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdPerl( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag );
char *botHelpPerl( void *tag );

BalancedBTree_t *db_get_perl( void );
void result_get_perl( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                      long insertid );
void cursesPerlDisplay( void *arg );
void perlSaveFunc( void *arg, int index, char *string );

typedef struct {
    char           *name;
    char           *fileName;
    int             preload;
    int             loaded;
    char           *args;
    char           *modName;
    LinkedList_t   *regexps;
    LinkedList_t   *commands;
} Perl_t;

bool perlUnload( char *name );
bool perlLoad( char *name );
bool perlLoadItem( Perl_t *perl );
void perlUnloadItem( Perl_t *perl );
void perlInitializeTree( BalancedBTreeItem_t *item );

#if 0
static int perl_LogPrint( lua_State *L );
static int perl_transmitMsg( lua_State *L );
static int perl_LoggedChannelMessage( lua_State *L );
static int perl_LoggedActionMessage( lua_State *L );
static int perl_regexp_add( lua_State *L );
static int perl_regexp_remove( lua_State *L );
static int perl_botCmd_add( lua_State *L );
static int perl_botCmd_remove( lua_State *L );
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
#endif

static PerlInterpreter *my_perl = NULL;

BalancedBTree_t *perlTree = NULL;
int              perlMenuId;

#define CURRENT_SCHEMA_PERL 1

static QueryTable_t defSchema[] = {
  { "CREATE TABLE `plugin_perl` (\n"
    "    `scriptName` VARCHAR( 64 ) NOT NULL ,\n"
    "    `fileName` VARCHAR( 64 ) NOT NULL ,\n"
    "    `preload` INT NOT NULL ,\n"
    "    `arguments` VARCHAR( 255 ) NOT NULL ,\n"
    "    PRIMARY KEY ( `scriptName` )\n"
    ") TYPE = MYISAM\n", NULL, NULL, FALSE }
};
static int defSchemaCount = NELEMENTS(defSchema);

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA_PERL] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } }
};

static QueryTable_t perlQueryTable[] = {
    /* 0 */
    { "SELECT scriptName, fileName, preload, arguments FROM plugin_perl",
      NULL, NULL, FALSE }
};

#if 0
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

static void perlTrashRegexp( Luascript_t *luascript, LuaRegexp_t *regexp );
static void perlTrashCommand( Luascript_t *luascript, LuaBotCmd_t *cmd );
void perlRegexpFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                     char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                     void *tag );
void perlCommandFunc( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                      char *msg, void *tag );
char *perlHelpFunc( void *tag );
#endif

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugin_initialize( char *args )
{
    static char            *command = "perl";
    char                    version[16];
    int                     retval;
    static char            *dummy_args[] = { "", "-e", "0" };
    SV                     *sv;

    LogPrintNoArg( LOG_NOTICE, "Initializing perl..." );
    snprintf( version, 16, "%d.%d.%d", PERL_REVISION, PERL_VERSION, 
              PERL_SUBVERSION );
    LogPrint( LOG_NOTICE, "Using Perl %s", version );
    versionAdd( "Perl", version );
    LogPrint( LOG_NOTICE, "Script path: %s", PLUGIN_PATH );

    db_check_schema( "dbSchemaPerl", "Perl", 
                     CURRENT_SCHEMA_PERL, defSchema, defSchemaCount,
                     schemaUpgrade );

    perlMenuId = cursesMenuItemAdd( 1, -1, "Perl", NULL, NULL );

    perlTree = db_get_perl();
    if( !perlTree ) {
        LogPrintNoArg( LOG_NOTICE, "No Perl scripts defined, unloading "
                                   "perl..." );
        return;
    }

    BalancedBTreeLock( perlTree );
    if( !perlTree->root ) {
        LogPrintNoArg( LOG_NOTICE, "No Perl scripts defined, unloading "
                                   "perl..." );
        BalancedBTreeUnlock( perlTree );
        return;
    }

    if( (my_perl = perl_alloc()) == NULL ) {
        LogPrintNoArg( LOG_CRIT, "No memory to start perl" );
        return;
    }
    perl_construct(my_perl);

    retval = perl_parse(my_perl, NULL, 3, dummy_args, NULL);
    if( retval ) {
        PL_perl_destruct_level = 0;
        perl_destruct(my_perl);
        perl_free(my_perl);
        LogPrintNoArg( LOG_CRIT, "Perl parse failed" );
        return;
    }

    sv = perl_eval_pv( EMBEDDING_PL, FALSE );
    if( !sv ) {
        /* This may not be the correct error checking! */
        LogPrintNoArg( LOG_CRIT, "Error bootstrapping embedded.pl" );
        return;
    }

    retval = perl_run(my_perl);
    if( retval ) {
        PL_perl_destruct_level = 0;
        perl_destruct(my_perl);
        perl_free(my_perl);
        LogPrintNoArg( LOG_CRIT, "Perl run failed" );
        return;
    }

    perlInitializeTree( perlTree->root );
    BalancedBTreeUnlock( perlTree );

    botCmd_add( (const char **)&command, botCmdPerl, botHelpPerl, NULL );
}

void plugin_shutdown( void )
{
    BalancedBTreeItem_t    *item;
    Perl_t                 *perl;

    LogPrintNoArg( LOG_NOTICE, "Removing perl..." );
    botCmd_remove( "perl" );

    versionRemove( "Perl" );

    cursesMenuItemRemove( 1, perlMenuId, "Perl" );
    if( !perlTree ) {
        return;
    }
    BalancedBTreeLock( perlTree );
    while( (item = perlTree->root) ) {
        perl = (Perl_t *)item->item;

        cursesMenuItemRemove( 2, perlMenuId, perl->name );
        if( perl->loaded ) {
            perlUnloadItem( perl );
        }
        BalancedBTreeRemove( perlTree, item, LOCKED, FALSE );

        free( perl->name );
        free( perl->fileName );
        free( perl->args );
        free( perl->modName );

        LinkedListLock( perl->regexps );
        LinkedListDestroy( perl->regexps );
        LinkedListLock( perl->commands );
        LinkedListDestroy( perl->commands );
        free( perl );
        free( item );
    }
    BalancedBTreeDestroy( perlTree );

    PL_perl_destruct_level = 0;
    perl_destruct(my_perl);
    perl_free(my_perl);
}

BalancedBTree_t *db_get_perl( void )
{
    BalancedBTree_t    *tree;
    pthread_mutex_t    *mutex;

    tree = BalancedBTreeCreate( BTREE_KEY_STRING );
    if( !tree ) {
        return( tree );
    }
    
    mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init( mutex, NULL );

    db_queue_query( 0, perlQueryTable, NULL, 0, result_get_perl, tree, mutex);
    pthread_mutex_unlock( mutex );
    pthread_mutex_destroy( mutex );
    free( mutex );

    return( tree );
}

void result_get_perl( MYSQL_RES *res, MYSQL_BIND *input, void *arg,
                      long insertid )
{
    Perl_t                 *perl;
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

        perl = (Perl_t *)malloc(sizeof(Perl_t));
        if( !perl ) {
            continue;
        }

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( !item ) {
            free( perl );
            continue;
        }

        memset( perl, 0, sizeof(Perl_t) );

        perl->name     = strdup(row[0]);
        perl->fileName = strdup(row[1]);
        perl->preload  = atoi(row[2]);
        perl->args     = strdup(row[3]);

        perl->regexps  = LinkedListCreate();
        perl->commands = LinkedListCreate();

        item->item = perl;
        item->key  = (void *)&perl->name;

        BalancedBTreeAdd( tree, item, LOCKED, FALSE );

        cursesMenuItemAdd( 2, perlMenuId, perl->name, cursesPerlDisplay, perl );
    }

    /* Rebalance the tree */
    BalancedBTreeAdd( tree, NULL, LOCKED, TRUE );

    BalancedBTreeUnlock( tree );
}


void perlInitializeTree( BalancedBTreeItem_t *item )
{
    Perl_t        *perl;

    if( !item ) {
        return;
    }

    perlInitializeTree( item->left );

    perl = (Perl_t *)item->item;
    if( perl->preload ) {
        perlLoadItem( perl );
    }

    perlInitializeTree( item->right );
}

bool perlLoad( char *name )
{
    BalancedBTreeItem_t    *item;
    Perl_t                 *perl;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( perlTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    perl = (Perl_t *)item->item;

    if( perl->loaded ) {
        return( false );
    }

    return( perlLoadItem( perl ) );
}

bool perlUnload( char *name )
{
    BalancedBTreeItem_t    *item;
    Perl_t                 *perl;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( perlTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    perl = (Perl_t *)item->item;

    if( !perl->loaded ) {
        return( false );
    }

    perlUnloadItem( perl );
    return( true );
}

bool perlLoadItem( Perl_t *perl )
{
    char                   *scriptfile;
    char                   *modname;
    char                   *initstring;
    int                     i;
    static char            *args[] = { "", NULL };

    scriptfile = (char *)malloc(strlen(PLUGIN_PATH) + 
                         strlen(perl->fileName) + 3 );
    sprintf( scriptfile, "%s/%s", PLUGIN_PATH, perl->fileName );

    modname = (char *)malloc(strlen(PERL_MODULE_PREFIX) + 
                             strlen(perl->fileName) + 3);
    sprintf( modname, "%s::%s", PERL_MODULE_PREFIX, perl->fileName );
    for( i = strlen(PERL_MODULE_PREFIX)+2; modname[i]; i++ ) {
        if( modname[i] == '.' && (modname[i+1] == 'P' || modname[i+1] == 'p') &&
            (modname[i+2] == 'L' || modname[i+2] == 'l') ) {
            modname[i] = '\0';
            break;
        }
    }
    perl->modName = strdup(modname);
    free( modname );

    LogPrint( LOG_NOTICE, "Loading Perl script %s from %s", perl->name, 
                          scriptfile );
    LogPrint( LOG_NOTICE, "Installing as Perl module %s", perl->modName );

    args[0] = scriptfile;
    call_argv("Embed::Persistent::eval_file", G_DISCARD | G_EVAL, args);

    /* check $@ */
    if(SvTRUE(ERRSV)) {
        LogPrint( LOG_CRIT, "Perl module %s error: %s", perl->modName,
                  SvPV(ERRSV,PL_na) );
    }

    free( scriptfile );

    initstring = (char *)malloc(strlen(perl->modName) + 13);
    sprintf( initstring, "%s::initialize", perl->modName );
    dSP;
    PUSHMARK(SP);
    call_pv( initstring, G_DISCARD | G_EVAL | G_NOARGS );

    free( initstring );

    perl->loaded = true;
    return( TRUE );
}

void perlUnloadItem( Perl_t *perl )
{
#if 0
    LinkedListItem_t       *item;
    LuaRegexp_t            *regexp;
    LuaBotCmd_t            *cmd;
#endif
    char                   *args[] = { "", NULL };
    char                   *string;

    if( !perl ) {
        return;
    }

    string = (char *)malloc(strlen(perl->modName) + 11);
    sprintf( string, "%s::shutdown", perl->modName );
    dSP;
    PUSHMARK(SP);
    call_pv( string, G_DISCARD | G_EVAL );

    LogPrint( LOG_NOTICE, "Unloading Perl script %s", perl->name );

    args[0] = perl->modName;
    call_argv("Embed::Persistent::unload_file", G_DISCARD | G_EVAL, args);

    perl->loaded = false;

#if 0
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
#endif
}


void botCmdPerl( IRCServer_t *server, IRCChannel_t *channel, char *who, 
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
        BalancedBTreeLock( perlTree );
        if( line && !strcmp( line, "all" ) ) {
            message = botCmdDepthFirst( perlTree->root, false );
        } else {
            message = botCmdDepthFirst( perlTree->root, true );
        }
        BalancedBTreeUnlock( perlTree );
    } else if( !strcmp( command, "load" ) && line ) {
        ret = perlLoad( line );
        message = (char *)malloc(strlen(line) + 36);
        if( ret ) {
            sprintf( message, "Loaded Perl script %s", line );
        } else {
            sprintf( message, "Perl script %s already loaded", line );
        }
    } else if( !strcmp( command, "unload" ) && line ) {
        ret = perlUnload( line );
        message = (char *)malloc(strlen(line) + 36);
        if( ret ) {
            sprintf( message, "Unloaded Perl script %s", line );
        } else {
            sprintf( message, "Perl script %s already unloaded", line );
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

char *botHelpPerl( void *tag )
{
    static char *help = "Loads, unloads and lists Perl scripted plugins "
                        "(requires authentication)  "
                        "Syntax: (in privmsg) perl load script/"
                        "unload script/list";
    
    return( help );
}

#if 0
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
#endif


static CursesFormItem_t perlFormItems[] = {
    { FIELD_LABEL, 1, 1, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_CHECKBOX, 12, 1, 0, 0, "[%c]", OFFSETOF(preload,Perl_t), 
      FA_BOOL, 3, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 4, 3, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 9, 3, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesCancel, NULL }
};
static int perlFormItemCount = NELEMENTS(perlFormItems);

void cursesPerlDisplay( void *arg )
{
    cursesFormDisplay( arg, perlFormItems, perlFormItemCount, perlSaveFunc );
}

void perlSaveFunc( void *arg, int index, char *string )
{
    Perl_t         *perl;

    perl = (Perl_t *)arg;

    if( index == -1 ) {
        if( perl->preload && !perl->loaded ) {
            perlLoadItem( perl );
        } else if( !perl->preload && perl->loaded ) {
            perlUnloadItem( perl );
        }
        return;
    }

    cursesSaveOffset( arg, index, perlFormItems, perlFormItemCount, string );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
