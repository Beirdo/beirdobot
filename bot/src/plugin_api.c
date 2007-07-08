/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2007 Gavin Hurlbut
 *
 *  The plugin portion of beirdobot is free software; you can redistribute it 
 *  and/or modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this software; if not, write to the 
 *    Free Software Foundation, Inc., 
 *    51 Franklin Street, Fifth Floor,
 *    Boston, MA  02110-1301  USA
 */

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2006 Gavin Hurlbut
* All rights reserved
*/

/* INCLUDE FILES */
#include "environment.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "structs.h"
#include "protos.h"
#include "balanced_btree.h"
#include "logging.h"
#define _DEFINE_PLUGINS
#include "plugins/plugin_list.h"


#ifndef PLUGIN_PATH
#define PLUGIN_PATH "./plugins"
#endif

/* INTERNAL FUNCTION PROTOTYPES */
void pluginInitializeTree( BalancedBTreeItem_t *item );
void pluginLoadItem( Plugin_t *plugin );
void pluginUnloadItem( Plugin_t *plugin );
void botCmdPlugin( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, void *tag );
void pluginUnloadTree( BalancedBTreeItem_t *node );
void pluginUnvisitTree( BalancedBTreeItem_t *node );
bool pluginFlushUnvisited( BalancedBTreeItem_t *node );
void pluginSaveFunc( void *arg, int index, char *string );

BalancedBTree_t *pluginTree;

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugins_initialize( void )
{
    static char        *command = "plugin";

    pluginTree = BalancedBTreeCreate( BTREE_KEY_STRING );
    if( !pluginTree ) {
        return;
    }

    db_check_plugins( DefaultPlugins, DefaultPluginCount );

    LogPrint( LOG_NOTICE, "Plugin path: %s", PLUGIN_PATH );

    BalancedBTreeLock( pluginTree );

    db_get_plugins( pluginTree );
    pluginInitializeTree( pluginTree->root );

    BalancedBTreeUnlock( pluginTree );

    botCmd_add( (const char **)&command, botCmdPlugin, NULL, NULL );
}

void plugins_sighup( void )
{
    BalancedBTreeLock( pluginTree );

    pluginUnvisitTree( pluginTree->root );
    db_get_plugins( pluginTree );
    pluginInitializeTree( pluginTree->root );
    while( pluginFlushUnvisited( pluginTree->root ) ) {
        /* 
         * Keep calling until nothing's been flushed.  This allows for the 
         * fact that the tree will shift around as we delete things, and the
         * recursion will be messed up by this.
         */
    }

    /* Rebalance the tree */
    BalancedBTreeAdd( pluginTree, NULL, LOCKED, TRUE );

    BalancedBTreeUnlock( pluginTree );
}

void pluginUnvisitTree( BalancedBTreeItem_t *node )
{
    Plugin_t       *plugin;

    if( !node ) {
        return;
    }

    pluginUnvisitTree( node->left );

    plugin = (Plugin_t *)node->item;
    plugin->visited = FALSE;
    
    pluginUnvisitTree( node->right );
}

bool pluginFlushUnvisited( BalancedBTreeItem_t *node )
{
    Plugin_t       *plugin;

    if( !node ) {
        return( FALSE );
    }

    if( pluginFlushUnvisited( node->left ) ) {
        /* Something was deleted on left side, restart at the root */
        return( TRUE );
    }

    plugin = (Plugin_t *)node->item;
    if( !plugin->visited ) {
        /* Remove it from the tree */
        BalancedBTreeRemove( node->btree, node, LOCKED, FALSE );

        if( plugin->loaded ) {
            pluginUnloadItem( plugin );
        }

        cursesMenuItemRemove( 2, MENU_PLUGINS, plugin->name );
        free( plugin->name );
        free( plugin->libName );
        free( plugin->args );
        free( plugin );
        free( node );
        return( TRUE );
    }

    if( pluginFlushUnvisited( node->right ) ) {
        /* Something was deleted on right side, restart at the root */
        return( TRUE );
    }

    return( FALSE );
}

void pluginInitializeTree( BalancedBTreeItem_t *item )
{
    Plugin_t   *plugin;

    if( !item ) {
        return;
    }

    pluginInitializeTree( item->left );

    plugin = (Plugin_t *)item->item;
    if( plugin->newPlugin ) {
        if( plugin->preload ) {
            pluginLoadItem( plugin );
        }
        plugin->newPlugin = FALSE;
    }

    pluginInitializeTree( item->right );
}

bool pluginLoad( char *name )
{
    BalancedBTreeItem_t    *item;
    Plugin_t               *plugin;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( pluginTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    plugin = (Plugin_t *)item->item;

    if( plugin->loaded ) {
        return( false );
    }

    pluginLoadItem( plugin );
    return( true );
}

bool pluginUnload( char *name )
{
    BalancedBTreeItem_t    *item;
    Plugin_t               *plugin;

    if( !name ) {
        return( false );
    }

    item = BalancedBTreeFind( pluginTree, (void *)&name, UNLOCKED );
    if( !item ) {
        return( false );
    }

    plugin = (Plugin_t *)item->item;

    if( !plugin->loaded ) {
        return( false );
    }

    pluginUnloadItem( plugin );
    return( true );
}

void pluginLoadItem( Plugin_t *plugin )
{
    char                   *libfile;
    char                   *error;

    libfile = (char *)malloc(strlen(PLUGIN_PATH) + 
                             strlen(plugin->libName) + 3 );
    sprintf( libfile, "%s/%s", PLUGIN_PATH, plugin->libName );

    LogPrint( LOG_NOTICE, "Loading plugin %s from %s", plugin->name, libfile );
    plugin->handle = dlopen( (const char *)libfile, RTLD_LAZY );
    free( libfile );

    if( !plugin->handle ) {
        LogPrint( LOG_CRIT, "%s", dlerror() );
        return;
    }

    /* Clear any errors */
    dlerror();
    *(void **)(&plugin->init) = dlsym( plugin->handle, "plugin_initialize" );
    if( (error = dlerror()) != NULL ) {
        LogPrint( LOG_CRIT, "%s", error );
        return;
    }

    *(void **)(&plugin->shutdown) = dlsym( plugin->handle, "plugin_shutdown" );
    if( (error = dlerror()) != NULL ) {
        LogPrint( LOG_CRIT, "%s", error );
        return;
    }

    plugin->init(plugin->args);
    plugin->loaded = true;
}

void pluginUnloadItem( Plugin_t *plugin )
{
    char       *error;

    if( !plugin ) {
        return;
    }

    plugin->shutdown();

    LogPrint( LOG_NOTICE, "Unloading plugin %s", plugin->name );
    dlclose( plugin->handle );
    plugin->handle = NULL;
    if( (error = dlerror()) != NULL ) {
        LogPrint( LOG_CRIT, "%s", error );
        return;
    }
    plugin->loaded = false;
}

void botCmdPlugin( IRCServer_t *server, IRCChannel_t *channel, char *who, 
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
        BalancedBTreeLock( pluginTree );
        if( line && !strcmp( line, "all" ) ) {
            message = botCmdDepthFirst( pluginTree->root, false );
        } else {
            message = botCmdDepthFirst( pluginTree->root, true );
        }
        BalancedBTreeUnlock( pluginTree );
    } else if( !strcmp( command, "load" ) && line ) {
        ret = pluginLoad( line );
        message = (char *)malloc(strlen(line) + 32);
        if( ret ) {
            sprintf( message, "Loaded module %s", line );
        } else {
            sprintf( message, "Module %s already loaded", line );
        }
    } else if( !strcmp( command, "unload" ) && line ) {
        ret = pluginUnload( line );
        message = (char *)malloc(strlen(line) + 32);
        if( ret ) {
            sprintf( message, "Unloaded module %s", line );
        } else {
            sprintf( message, "Module %s already unloaded", line );
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

void pluginUnloadAll( void )
{
    if( !pluginTree ) {
        return;
    }

    BalancedBTreeLock( pluginTree );
    pluginUnloadTree( pluginTree->root );
    BalancedBTreeUnlock( pluginTree );
}
        
void pluginUnloadTree( BalancedBTreeItem_t *node )
{
    Plugin_t               *plugin;

    if( !node ) {
        return;
    }

    pluginUnloadTree( node->left );

    plugin = (Plugin_t *)node->item;

    if( plugin->loaded ) {
        pluginUnloadItem( plugin );
    }

    pluginUnloadTree( node->right );
}

static CursesFormItem_t pluginFormItems[] = {
    { FIELD_LABEL, 1, 1, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_CHECKBOX, 12, 1, 0, 0, "[%c]", OFFSETOF(preload,Plugin_t), FA_BOOL,
      3, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 4, 3, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 9, 3, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 },
      cursesCancel, NULL }
};
static int pluginFormItemCount = NELEMENTS(pluginFormItems);

void cursesPluginDisplay( void *arg )
{
    cursesFormDisplay( arg, pluginFormItems, pluginFormItemCount,
                       pluginSaveFunc );
}

void pluginSaveFunc( void *arg, int index, char *string )
{
    Plugin_t               *plugin;

    plugin = (Plugin_t *)arg;

    if( index == -1 ) {
        LogPrint( LOG_DEBUG, "plugin: %p - complete", arg );
        if( plugin->preload && !plugin->loaded ) {
            pluginLoadItem( plugin );
        } else if( !plugin->preload && plugin->loaded ) {
            pluginUnloadItem( plugin );
        }
        return;
    }

    cursesSaveOffset( arg, index, pluginFormItems, pluginFormItemCount, 
                      string );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
