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
#include "environment.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "structs.h"
#include "protos.h"
#include "balanced_btree.h"


#define PLUGIN_PATH "./plugins"

/* INTERNAL FUNCTION PROTOTYPES */
void pluginInitializeTree( BalancedBTreeItem_t *item );
void pluginLoadItem( Plugin_t *plugin );
void pluginUnloadItem( Plugin_t *plugin );
void botCmdPlugin( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg );

BalancedBTree_t *pluginTree;

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugins_initialize( void )
{
    static char        *command = "plugin";

    pluginTree = db_get_plugins();
    if( !pluginTree ) {
        return;
    }

    BalancedBTreeLock( pluginTree );
    pluginInitializeTree( pluginTree->root );
    BalancedBTreeUnlock( pluginTree );

    botCmd_add( (const char **)&command, botCmdPlugin, NULL );
}

void pluginInitializeTree( BalancedBTreeItem_t *item )
{
    Plugin_t   *plugin;

    if( !item ) {
        return;
    }

    pluginInitializeTree( item->left );

    plugin = (Plugin_t *)item->item;
    if( plugin->preload ) {
        pluginLoadItem( plugin );
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

    printf( "Loading plugin %s from %s\n", plugin->name, libfile );
    plugin->handle = dlopen( (const char *)libfile, RTLD_LAZY );
    free( libfile );

    if( !plugin->handle ) {
        fprintf( stderr, "%s\n", dlerror() );
        return;
    }

    /* Clear any errors */
    dlerror();
    *(void **)(&plugin->init) = dlsym( plugin->handle, "plugin_initialize" );
    if( (error = dlerror()) != NULL ) {
        fprintf( stderr, "%s\n", error );
        return;
    }

    *(void **)(&plugin->shutdown) = dlsym( plugin->handle, "plugin_shutdown" );
    if( (error = dlerror()) != NULL ) {
        fprintf( stderr, "%s\n", error );
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

    printf( "Unloading plugin %s\n", plugin->name );
    dlclose( plugin->handle );
    plugin->handle = NULL;
    if( (error = dlerror()) != NULL ) {
        fprintf( stderr, "%s\n", error );
        return;
    }
    plugin->loaded = false;
}

void botCmdPlugin( IRCServer_t *server, IRCChannel_t *channel, char *who, 
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
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, notauth);
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

    BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message);

    free( message );
    free( command );
}



/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
