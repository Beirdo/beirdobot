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
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "protos.h"
#include "structs.h"
#include "queue.h"
#include "logging.h"


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    TxType_t    type;
    char       *channel;
    char       *message;
} TransmitItem_t;

void *transmit_thread(void *arg)
{
    TransmitItem_t     *item;
    char                string[MAX_STRING_LENGTH];
    struct timespec     ts;
    struct timeval      now;
    IRCServer_t        *server;
    time_t              sendTime;
    char               *msg;
    int                 len;

    server   = (IRCServer_t *)arg;
    sendTime = 0;

    /* One second delay */
    ts.tv_sec = 1;
    ts.tv_nsec = 0L;

    LogPrint( LOG_NOTICE, "Starting transmit thread - %s", 
                          server->txThreadName );

    while( !GlobalAbort ) {
        item = (TransmitItem_t *)QueueDequeueItem( server->txQueue, -1 );
        if( !item ) {
            continue;
        }

        gettimeofday( &now, NULL );
        if( sendTime < now.tv_sec ) {
            sendTime = now.tv_sec;
        }

        if( sendTime - now.tv_sec > 10 ) {
            LogPrint( LOG_NOTICE, "Delaying %ld seconds", 
                      sendTime - now.tv_sec - 10 );
            sleep( sendTime - now.tv_sec - 10 );
        }

        msg = NULL;

        switch( item->type ) {
        case TX_NOTICE:
            msg = BN_MakeMessage(NULL, "NOTICE", item->message);
            break;
        case TX_MESSAGE:
        case TX_PRIVMSG:
            snprintf(string, MAX_STRING_LENGTH, "%s :%s", item->channel,
                     item->message);
            msg = BN_MakeMessage(NULL, "PRIVMSG", string);
            break;
        case TX_ACTION:
            snprintf(string, MAX_STRING_LENGTH, "%s :%cACTION %s%c",
                     item->channel, 1, item->message, 1);
            msg = BN_MakeMessage(NULL, "PRIVMSG", string);
            break;
        case TX_JOIN:
            snprintf(string, MAX_STRING_LENGTH, "%s%s%s", item->channel, 
                     (item->message ? " " : ""), 
                     (item->message ? item->message : "" ) );
            msg = BN_MakeMessage(NULL, "JOIN", string);
            break;
        case TX_PASSWORD:
            msg = BN_MakeMessage(NULL, "PASS", item->message);
            break;
        case TX_NICK:
            len = sizeof(server->ircInfo.Nick);
            strncpy(server->ircInfo.Nick, item->message, len - 1);
            server->ircInfo.Nick[len-1] = 0;
            msg = BN_MakeMessage(NULL, "NICK", item->message);
            break;
        case TX_REGISTER:
            /* channel -> username, message -> realname */
            snprintf(string, MAX_STRING_LENGTH, "%s 0 0 : %s",
                     item->channel, item->message);
            msg = BN_MakeMessage(NULL, "USER", string);
            break;
        case TX_WHO:
            msg = BN_MakeMessage(NULL, "WHO", item->channel);
            break;
        case TX_QUIT:
            snprintf(string, MAX_STRING_LENGTH, ":%s", item->message);
            msg = BN_MakeMessage(NULL, "QUIT", string);
            break;
        }

        if( msg ) {
            BN_SendMessage( &server->ircInfo, msg, BN_LOW_PRIORITY );
            sendTime += 2 + (strlen(msg) / 120);
        }

        gettimeofday( &now, NULL );
        LogPrint( LOG_NOTICE, "Sendtime: %ld/%ld", sendTime, 
                  sendTime - now.tv_sec );

        if( item->message ) {
            free( item->message );
        }
        free( item );
    }

    LogPrint( LOG_NOTICE, "Ending transmit thread - %s", 
                          server->txThreadName );
    return(NULL);
}

void transmitMsg( IRCServer_t *server, TxType_t type, char *channel, 
               char *message )
{
    TransmitItem_t     *item;

    if( !server || !channel ) {
        return;
    }

    item = (TransmitItem_t *)malloc(sizeof(TransmitItem_t));
    if( !item ) {
        return;
    }

    item->type    = type;
    item->channel = channel;
    if( message ) {
        item->message = strdup( message );
    } else {
        item->message = NULL;
    }

    QueueEnqueueItem( server->txQueue, item );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
