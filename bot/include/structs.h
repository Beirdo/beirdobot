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
#include <mysql.h>
#include "botnet.h"
#include "environment.h"
#include "linked_list.h"
#include "balanced_btree.h"
#include "queue.h"

static char interthread_h_ident[] _UNUSED_ = 
    "$Id$";


typedef struct {
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
    int                 floodInterval;
    int                 floodMaxTime;
    int                 floodBuffer;
    int                 floodMaxLine;
    LinkedList_t       *floodList;
    BN_TInfo            ircInfo;
    pthread_t           threadId;
    char               *threadName;
    pthread_t           txThreadId;
    char               *txThreadName;
    QueueObject_t      *txQueue;
    bool                threadAbort;
    bool                enabled;
    bool                visited;
    bool                newServer;
    char               *menuText;
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
    bool                enabled;
    bool                visited;
    bool                newChannel;
    char               *menuText;
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
                              char *who, char *msg, void *tag );
typedef char * (*BotCmdHelpFunc_t)( void *tag );

typedef struct {
    char               *command;
    BotCmdFunc_t        func;
    BotCmdHelpFunc_t    helpFunc;
    void               *tag;
} BotCmd_t;

typedef struct {
    char           *name;
    char           *libName;
    int             preload;
    int             loaded;
    bool            newPlugin;
    bool            visited;
    char           *args;
    void           *handle;
    void          (*init)(char *args);
    void          (*shutdown)(void);
} Plugin_t;

typedef void (*RegexpFunc_t)( IRCServer_t *server, IRCChannel_t *channel, 
                              char *who, char *msg, IRCMsgType_t type,
                              int *ovector, int ovecsize, void *tag );
typedef struct {
    LinkedListItem_t    item;
    const char         *channelRegexp;
    const char         *contentRegexp;
    pcre               *reChannel;
    pcre_extra         *peChannel;
    pcre               *reContent;
    pcre_extra         *peContent;
    RegexpFunc_t        func;
    void               *tag;
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
    LT_SYSLOG,
    LT_NCURSES
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


typedef enum {
    TX_NOTICE,
    TX_MESSAGE,
    TX_ACTION,
    TX_PRIVMSG,
    TX_JOIN,
    TX_PASSWORD,
    TX_NICK,
    TX_REGISTER,
    TX_WHO,
    TX_QUIT,
    TX_PART
} TxType_t;

typedef struct {
    char           *pluginName;
    char           *libName;
    int             preload;
    char           *arguments;
} PluginDef_t;

typedef struct {
    LinkedListItem_t    item;
    time_t              timeWake;
    int                 bytes;
} FloodListItem_t;

#if ( MYSQL_VERSION_ID < 40102 ) 

#define NO_PREPARED_STATEMENTS

/*
 * Adapted libmysqlclient14 (4.1.x) mysql_com.h
 */
#ifndef MYSQL_TYPE_DECIMAL

#define MYSQL_TYPE_DECIMAL     FIELD_TYPE_DECIMAL
#define MYSQL_TYPE_TINY        FIELD_TYPE_TINY
#define MYSQL_TYPE_SHORT       FIELD_TYPE_SHORT
#define MYSQL_TYPE_LONG        FIELD_TYPE_LONG
#define MYSQL_TYPE_FLOAT       FIELD_TYPE_FLOAT
#define MYSQL_TYPE_DOUBLE      FIELD_TYPE_DOUBLE
#define MYSQL_TYPE_NULL        FIELD_TYPE_NULL
#define MYSQL_TYPE_TIMESTAMP   FIELD_TYPE_TIMESTAMP
#define MYSQL_TYPE_LONGLONG    FIELD_TYPE_LONGLONG
#define MYSQL_TYPE_INT24       FIELD_TYPE_INT24
#define MYSQL_TYPE_DATE        FIELD_TYPE_DATE
#define MYSQL_TYPE_TIME        FIELD_TYPE_TIME
#define MYSQL_TYPE_DATETIME    FIELD_TYPE_DATETIME
#define MYSQL_TYPE_YEAR        FIELD_TYPE_YEAR
#define MYSQL_TYPE_NEWDATE     FIELD_TYPE_NEWDATE
#define MYSQL_TYPE_ENUM        FIELD_TYPE_ENUM
#define MYSQL_TYPE_SET         FIELD_TYPE_SET
#define MYSQL_TYPE_TINY_BLOB   FIELD_TYPE_TINY_BLOB
#define MYSQL_TYPE_MEDIUM_BLOB FIELD_TYPE_MEDIUM_BLOB
#define MYSQL_TYPE_LONG_BLOB   FIELD_TYPE_LONG_BLOB
#define MYSQL_TYPE_BLOB        FIELD_TYPE_BLOB
#define MYSQL_TYPE_VAR_STRING  FIELD_TYPE_VAR_STRING
#define MYSQL_TYPE_STRING      FIELD_TYPE_STRING
#define MYSQL_TYPE_CHAR        FIELD_TYPE_TINY
#define MYSQL_TYPE_INTERVAL    FIELD_TYPE_ENUM
#define MYSQL_TYPE_GEOMETRY    FIELD_TYPE_GEOMETRY

#endif

/*
 * From libmysqlclient14 (4.1.x) mysql.h
 */
typedef struct st_mysql_bind
{
  unsigned long *length;          /* output length pointer */
  my_bool       *is_null;         /* Pointer to null indicator */
  void          *buffer;          /* buffer to get/put data */
  enum enum_field_types buffer_type;    /* buffer type */
  unsigned long buffer_length;    /* buffer length, must be set for str/binary */  
} MYSQL_BIND;

#define MYSQL_STMT void

#endif

struct _QueryItem_t;
typedef void (*QueryChainFunc_t)( MYSQL_RES *res, struct _QueryItem_t *item ); 

typedef struct {
    const char         *queryPattern;
    QueryChainFunc_t    queryChainFunc;
    MYSQL_STMT         *queryStatement;
    bool                queryPrepared;
} QueryTable_t;

#define MAX_SCHEMA_QUERY 100
typedef QueryTable_t SchemaUpgrade_t[MAX_SCHEMA_QUERY];


typedef void (*QueryResFunc_t)( MYSQL_RES *res, MYSQL_BIND *input, void *arg );

typedef struct _QueryItem_t {
    int                 queryId;
    QueryTable_t       *queryTable;
    MYSQL_BIND         *queryData;
    int                 queryDataCount;
    QueryResFunc_t      queryCallback;
    void               *queryCallbackArg;
    pthread_mutex_t    *queryMutex;
    unsigned int        querySequence;
} QueryItem_t;

typedef void (*SigFunc_t)( int, void * );

typedef void (*CursesMenuFunc_t)(void *);

typedef enum {
    WINDOW_HEADER,
    WINDOW_MENU1,
    WINDOW_MENU2,
    WINDOW_DETAILS,
    WINDOW_DETAILS_FORM,
    WINDOW_LOG,
    WINDOW_TAILER,
    WINDOW_COUNT
} CursesWindow_t;

typedef enum {
    MENU_SYSTEM,
    MENU_SERVERS,
    MENU_CHANNELS,
    MENU_DATABASE,
    MENU_PLUGINS
} CursesMenuNum_t;

typedef enum {
    ALIGN_LEFT,
    ALIGN_RIGHT,
    ALIGN_CENTER,       /* Center in window */
    ALIGN_FROM_CENTER,  /* Align left side of text to offset from center */
    ALIGN_WRAP          /* left aligned, wrapped */
} CursesTextAlign_t;

typedef bool (*CursesKeyhandleFunc_t)(int);

typedef union {
    int         minLen;
    struct {
        char  **stringList;
        int     caseSensitive;
        int     partialMatch;
    } enumArgs;
    struct {
        int     precision;
        long    minValue;
        long    maxValue;
    } integerArgs;
    struct {
        int     precision;
        double  minValue;
        double  maxValue;
    } numericArgs;
    char       *regexp;
} CursesFieldTypeArgs_t;

typedef enum {
    FIELD_LABEL,
    FIELD_FIELD,
    FIELD_BUTTON
} CursesFieldType_t;


#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
