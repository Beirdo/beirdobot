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

/*
 * HEADER--------------------------------------------------- 
 * $Id$ 
 *
 * Copyright 2006 Gavin Hurlbut 
 * All rights reserved 
 * 
 */

#include "environment.h"
#include "botnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include "protos.h"
#include "structs.h"
#include "queue.h"
#include "logging.h"


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

QueueObject_t  *NotifyQ;
pthread_t       notifyThreadId;

void *notify_thread(void *arg);


void notify_start(void)
{
    thread_create( &notifyThreadId, notify_thread, NULL, "thread_notify" );
}

void *notify_thread(void *arg)
{
    NotifyItem_t       *item;
    char                string[MAX_STRING_LENGTH];
    struct timespec     ts;

    NotifyQ = QueueCreate(2048);

    /* One second delay */
    ts.tv_sec = 1;
    ts.tv_nsec = 0L;

    LogPrintNoArg( LOG_NOTICE, "Starting notify thread" );

    while( !GlobalAbort ) {
        item = (NotifyItem_t *)QueueDequeueItem( NotifyQ, -1 );
        if( !item ) {
            continue;
        }

        LogPrint( LOG_INFO, "Notifying %s from %s", item->nick, 
                  item->channel->fullspec );

        snprintf( string, MAX_STRING_LENGTH,
                  "%s :This channel (%s) is logged -- %s", item->nick, 
                  item->channel->channel, item->channel->url );

        BN_SendMessage( &item->server->ircInfo, 
                        BN_MakeMessage(NULL, "NOTICE", string),
                        BN_LOW_PRIORITY);
        db_notify_nick( item->channel, item->nick );

        free( item->nick );
        free( item );

        nanosleep( &ts, NULL );
    }

    LogPrintNoArg( LOG_NOTICE, "Ending notify thread" );
    return(NULL);
}

void send_notice( IRCChannel_t *channel, char *nick )
{
    NotifyItem_t       *item;

    if( !channel || !nick ) {
        return;
    }

    item = (NotifyItem_t *)malloc(sizeof(NotifyItem_t));
    if( !item ) {
        return;
    }

    item->server  = channel->server;
    item->channel = channel;
    item->nick    = strdup(nick);

    QueueEnqueueItem( NotifyQ, item );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
