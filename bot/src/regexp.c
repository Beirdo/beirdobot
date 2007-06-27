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
#include <pcre.h>
#include "botnet.h"
#include "environment.h"
#include "structs.h"
#include "linked_list.h"
#include "logging.h"

/* INTERNAL FUNCTION PROTOTYPES */

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static char *channelsAll = "^.*$";
LinkedList_t   *regexpList;

void regexp_initialize( void )
{
    regexpList = LinkedListCreate();
}

void regexp_add( const char *channelRegexp, const char *contentRegexp, 
                 RegexpFunc_t func, void *tag )
{
    Regexp_t    *item;
    const char  *error;
    int          erroffset;

    item = (Regexp_t *)malloc(sizeof(Regexp_t));
    if( !item || !func ) {
        return;
    }

    if( !channelRegexp ) {
        channelRegexp = channelsAll;
    }

    item->tag = tag;

    item->channelRegexp = channelRegexp;
    item->contentRegexp = contentRegexp;

    item->reChannel = pcre_compile( channelRegexp, 0, &error, &erroffset, 
                                    NULL );
    if( error ) {
        LogPrint( LOG_CRIT, "%s", error );
        return;
    }

    item->peChannel = pcre_study( item->reChannel, 0, &error );
    if( error ) {
        LogPrint( LOG_CRIT, "%s", error );
        free( item->reChannel );
        return;
    }

    item->reContent = pcre_compile( contentRegexp, 0, &error, &erroffset,
                                    NULL );
    if( error ) {
        LogPrint( LOG_CRIT, "%s", error );
        free( item->reChannel );
        free( item->peChannel );
        return;
    }

    item->peContent = pcre_study( item->reContent, 0, &error );
    if( error ) {
        LogPrint( LOG_CRIT, "%s", error );
        free( item->reChannel );
        free( item->peChannel );
        free( item->reContent );
        return;
    }

    item->func      = func;

    LinkedListAdd( regexpList, (LinkedListItem_t *)item, UNLOCKED, AT_TAIL );
}

void regexp_remove( char *channelRegexp, char *contentRegexp )
{
    LinkedListItem_t   *item;
    Regexp_t           *regexp;

    if( !channelRegexp ) {
        channelRegexp = channelsAll;
    }

    LinkedListLock( regexpList );

    for( item = regexpList->head; item; item = item->next ) {
        regexp = (Regexp_t *)item;

        if( !strcmp( channelRegexp, regexp->channelRegexp ) &&
            !strcmp( contentRegexp, regexp->contentRegexp ) ) {
            LinkedListRemove( regexpList, item, LOCKED );

            free( regexp->reChannel );
            free( regexp->peChannel );
            free( regexp->reContent );
            free( regexp->peContent );
            free( item );
            LinkedListUnlock( regexpList );
            return;
        }
    }

    LinkedListUnlock( regexpList );
}

void regexp_parse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg, IRCMsgType_t type )
{
    LinkedListItem_t   *item;
    Regexp_t           *regexp;
    int                 rc;
    int                 lenMsg;
    int                 lenChan;
    int                 ovector[30];
    int                 start;

    if( !channel ) {
        return;
    }

    lenMsg  = strlen( msg );
    lenChan = strlen( channel->fullspec );

    LinkedListLock( regexpList );

    for( item = regexpList->head; item; item = item->next ) {
        regexp = (Regexp_t *)item;

        rc = pcre_exec( regexp->reChannel, regexp->peChannel,
                        channel->fullspec, lenChan, 0, 0, ovector, 30 );
        if( rc < 0 ) {
            /* Channels don't match */
            continue;
        }

        for( rc = 1, start = 0; rc > 0; start = ovector[1] ) {
            rc = pcre_exec( regexp->reContent, regexp->peContent, msg, lenMsg,
                            start, 0, ovector, 30 );
            if( rc >= 0 ) {
                /* We got a channel and content match, call the function */
                regexp->func( server, channel, who, msg, type, ovector, rc,
                              regexp->tag );
            }
        }
    }

    LinkedListUnlock( regexpList );
}

char *regexp_substring( char *msg, int *ovector, int stringcount,
                        int stringnum )
{
    char        *string;

    if( pcre_get_substring( msg, ovector, stringcount, stringnum, 
                            (const char **)&string ) < 0 ) {
        return( NULL );
    } else {
        return( string );
    }
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
