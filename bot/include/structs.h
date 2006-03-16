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

#ifndef structs_h_
#define structs_h_

#include <pthread.h>
#include <pcre.h>
#include "botnet.h"
#include "environment.h"
#include "linked_list.h"
#include "balanced_btree.h"

static char interthread_h_ident[] _UNUSED_ = 
    "$Id$";


typedef struct {
    LinkedListItem_t    item;
    LinkedList_t       *channels;
    BalancedBTree_t    *channelName;
    BalancedBTree_t    *channelNum;
    int                 serverId;
    char               *server;
    uint16              port;
    char               *password;
    char               *nick;
    char               *username;
    char               *realname;
    char               *nickserv;
    char               *nickservmsg;
    BN_TInfo            ircInfo;
    pthread_t           threadId;
    char               *threadName;
} IRCServer_t;

typedef struct {
    LinkedListItem_t    item;
    BalancedBTreeItem_t itemName;
    BalancedBTreeItem_t itemNum;
    int                 channelId;
    IRCServer_t        *server;
    char               *channel;
    char               *fullspec;
    char               *url;
    int                 notifywindow;
    char                cmdChar;
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

typedef void (*BotCmdFunc_t)( IRCServer_t *server, IRCChannel_t *channel, 
                              char *who, char *msg );
typedef char * (*BotCmdHelpFunc_t)( void );

typedef struct {
    char               *command;
    BotCmdFunc_t        func;
    BotCmdHelpFunc_t    helpFunc;
} BotCmd_t;

typedef struct {
    char           *name;
    char           *libName;
    int             preload;
    int             loaded;
    char           *args;
    void           *handle;
    void          (*init)(char *args);
    void          (*shutdown)(void);
} Plugin_t;

typedef void (*RegexpFunc_t)( IRCServer_t *server, IRCChannel_t *channel, 
                              char *who, char *msg, IRCMsgType_t type,
                              int *ovector, int ovecsize );
typedef struct {
    LinkedListItem_t    item;
    const char         *channelRegexp;
    const char         *contentRegexp;
    pcre               *reChannel;
    pcre_extra         *peChannel;
    pcre               *reContent;
    pcre_extra         *peContent;
    RegexpFunc_t        func;
} Regexp_t;

typedef enum {
    HIST_END = -2,
    HIST_START = -1,
    HIST_INITIAL = 0,
    HIST_JOIN,
    HIST_LEAVE
} NickHistory_t;

typedef struct {
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    char               *nick;
} NotifyItem_t;

typedef enum {
    AUTH_NONE,
    AUTH_CHALLENGE,
    AUTH_ACCEPTED,
    AUTH_REJECTED,
    AUTH_TIMEDOUT,
    AUTH_DISCONNECT
} AuthState_t;

typedef struct {
    LinkedListItem_t    item;
    IRCServer_t        *server;
    char               *nick;
    char               *digest;
    char               *seed;
    int                 count;
    char               *hash;
    AuthState_t         state;
    unsigned int        wakeTime;
} AuthData_t;

typedef enum
{
    LT_CONSOLE,
    LT_FILE,
    LT_SYSLOG
} LogFileType_t;

/* Log File Descriptor Chain */
typedef struct
{
    LinkedListItem_t linkage;
    int fd;
    LogFileType_t type;
    bool aborted;
    union 
    {
        char *filename;
    } identifier;
} LogFileChain_t;


#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
