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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "environment.h"
#include "structs.h"
#include "protos.h"
#include "balanced_btree.h"


#define PLUGIN_PATH "./plugins"

/* INTERNAL FUNCTION PROTOTYPES */
void pluginInitializeTree( BalancedBTreeItem_t *item );
void pluginLoad( Plugin_t *plugin );

BalancedBTree_t *pluginTree;

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void plugins_initialize( void )
{
    pluginTree = db_get_plugins();
    if( !pluginTree ) {
        return;
    }

    BalancedBTreeLock( pluginTree );
    pluginInitializeTree( pluginTree->root );
    BalancedBTreeUnlock( pluginTree );
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
        pluginLoad( plugin );
    }

    pluginInitializeTree( item->right );
}

void pluginLoad( Plugin_t *plugin )
{
    char       *libfile;
    char       *error;

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
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
