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
*
*/

#include "environment.h"
#include "botnet.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "protos.h"
#include "balanced_btree.h"
#include "logging.h"


static char ident[] _UNUSED_= 
    "$Id$";

BalancedBTree_t    *ThreadTree = NULL;


void thread_create( pthread_t *pthreadId, void * (*routine)(void *), 
                    void *arg, char *name )
{
    pthread_create( pthreadId, NULL, routine, arg );

    thread_register( pthreadId, name );
}

void thread_register( pthread_t *pthreadId, char *name )
{
    BalancedBTreeItem_t    *item;

    if( !ThreadTree ) {
        ThreadTree = BalancedBTreeCreate( BTREE_KEY_PTHREAD );
        if( !ThreadTree ) {
            fprintf( stderr, "Couldn't create thread tree!\n" );
            _exit( 1 );
        }
    }

    item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
    item->item = (void *)name;
    item->key  = (void *)pthreadId;

    BalancedBTreeAdd( ThreadTree, item, UNLOCKED, true );
    LogPrint( LOG_INFO, "Added Thread %ld as \"%s\"", *pthreadId, name );
}

char *thread_name( pthread_t pthreadId )
{
    BalancedBTreeItem_t    *item;

    item = BalancedBTreeFind( ThreadTree, (void *)&pthreadId, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    return( (char *)item->item );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

