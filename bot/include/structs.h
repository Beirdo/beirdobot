/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  havokmud is free software; you can redistribute it and/or modify
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

#ifndef structs_h_
#define structs_h_

#include <pthread.h>
#include "botnet.h"
#include "environment.h"
#include "linked_list.h"

static char interthread_h_ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    LinkedListItem_t    item;
    LinkedList_t       *channels;
    int                 serverId;
    char               *server;
    uint16              port;
    char               *nick;
    char               *username;
    char               *realname;
    char               *nickserv;
    char               *nickservmsg;
    BN_TInfo            ircInfo;
    pthread_t           threadId;
} IRCServer_t;

typedef struct {
    LinkedListItem_t    item;
    int                 channelId;
    IRCServer_t        *server;
    char               *channel;
    char               *url;
    int                 notifywindow;
    bool                joined;
} IRCChannel_t;

typedef enum {
    TYPE_MESSAGE,
    TYPE_ACTION,
    TYPE_TOPIC,
    TYPE_KICK,
    TYPE_MODE,
    TYPE_NICK,
    TYPE_JOIN,
    TYPE_PART,
    TYPE_QUIT
} IRCMsgType_t;

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
