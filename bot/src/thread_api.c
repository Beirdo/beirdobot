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
#include <signal.h>
#include "protos.h"
#include "balanced_btree.h"
#include "logging.h"


static char ident[] _UNUSED_= 
    "$Id$";

BalancedBTree_t    *ThreadTree = NULL;
extern pthread_t    mainThreadId;
extern pthread_t    cursesOutThreadId;

typedef struct {
    pthread_t          *threadId;
    char               *name;
    ThreadCallback_t   *callbacks;
} Thread_t;

void ThreadRecurseKill( BalancedBTreeItem_t *node, int signum );

void thread_create( pthread_t *pthreadId, void * (*routine)(void *), 
                    void *arg, char *name, ThreadCallback_t *callbacks )
{
    pthread_create( pthreadId, NULL, routine, arg );
    thread_register( pthreadId, name, callbacks );
}

void thread_register( pthread_t *pthreadId, char *name, 
                      ThreadCallback_t *callbacks )
{
    BalancedBTreeItem_t    *item;
    Thread_t               *thread;

    if( !ThreadTree ) {
        ThreadTree = BalancedBTreeCreate( BTREE_KEY_PTHREAD );
        if( !ThreadTree ) {
            fprintf( stderr, "Couldn't create thread tree!\n" );
            _exit( 1 );
        }
    }

    thread = (Thread_t *)malloc(sizeof(Thread_t));
    thread->threadId   = pthreadId;
    thread->name       = name;
    thread->callbacks  = callbacks;

    item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
    item->item = (void *)thread;
    item->key  = (void *)thread->threadId;

    BalancedBTreeAdd( ThreadTree, item, UNLOCKED, true );
    LogPrint( LOG_INFO, "Added Thread %ld as \"%s\"", *pthreadId, name );
}

char *thread_name( pthread_t pthreadId )
{
    BalancedBTreeItem_t    *item;
    Thread_t               *thread;

    item = BalancedBTreeFind( ThreadTree, (void *)&pthreadId, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    thread = (Thread_t *)item->item;

    return( thread->name );
}

void thread_deregister( pthread_t pthreadId )
{
    BalancedBTreeItem_t    *item;
    Thread_t               *thread;

    item = BalancedBTreeFind( ThreadTree, (void *)&pthreadId, UNLOCKED );
    if( !item ) {
        return;
    }

    BalancedBTreeRemove( ThreadTree, item, UNLOCKED, true );
    thread = (Thread_t *)item->item;

    LogPrint( LOG_INFO, "Removed Thread %ld as \"%s\"", pthreadId, 
                        (char *)thread->name );

    free( thread );
    free( item );

    if( !pthread_equal( pthread_self(), pthreadId ) ) {
        pthread_join( pthreadId, NULL );
    }
}


void ThreadAllKill( int signum )
{
    BalancedBTreeLock( ThreadTree );

    ThreadRecurseKill( ThreadTree->root, signum );

    BalancedBTreeUnlock( ThreadTree );
}

void ThreadRecurseKill( BalancedBTreeItem_t *node, int signum )
{
    Thread_t       *thread;

    if( !node ) {
        return;
    }

    ThreadRecurseKill( node->left, signum );

    thread = (Thread_t *)node->item;

    if( !pthread_equal( *thread->threadId, mainThreadId ) ) {
        switch( signum ) {
        case SIGUSR2:
        case SIGHUP:
        case SIGWINCH:
#if 0
            LogPrint( LOG_DEBUG, "Killing thread %s with signal %d",
                                 thread->name, signum );
#endif
            pthread_kill( *thread->threadId, signum );
            break;
        default:
            break;
        }
    }

    ThreadRecurseKill( node->right, signum );
}

SigFunc_t ThreadGetHandler( pthread_t threadId, int signum, void **parg )
{
    BalancedBTreeItem_t    *item;
    Thread_t               *thread;

    item = BalancedBTreeFind( ThreadTree, (void *)&threadId, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    thread = (Thread_t *)item->item;

    if( parg ) {
        *parg = NULL;
    }

    switch( signum ) {
    case SIGUSR2:
        return( do_backtrace );
        break;
    case SIGHUP:
        if( parg ) {
            if( thread->callbacks ) {
                *parg = thread->callbacks->sighupArg;
            } else {
                *parg = NULL;
            }
        }

        if( !thread->callbacks ) {
            return( NULL );
        }

        return( thread->callbacks->sighupFunc );
        break;
    case SIGWINCH:
        if( !Daemon && pthread_equal( threadId, cursesOutThreadId ) ) {
            return( cursesSigwinch );
        }
        break;
    default:
        break;
    }

    return( NULL );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

