/*
 *  This file is part of the beirdonet package
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

#ifndef protos_h_
#define protos_h_

#include "linked_list.h"
#include "structs.h"

/* CVS generated ID string (optional for h files) */
static char protos_h_ident[] _UNUSED_ = 
    "$Id$";

/* Externals */
extern char   *mysql_host;
extern uint16  mysql_portnum;
extern char   *mysql_user;
extern char   *mysql_password;
extern char   *mysql_db;
extern LinkedList_t   *ServerList;
extern bool verbose;
extern bool Daemon;


/* Prototypes */
const char *svn_version(void);
void bot_start(void);
void db_setup(void);
void db_load_servers(void);
void db_load_channels(void);
void db_add_logentry( IRCChannel_t *channel, char *nick, IRCMsgType_t msgType, 
                      char *text );
void db_update_nick( IRCChannel_t *channel, char *nick, bool present, 
                     bool extract );
void db_flush_nicks( IRCChannel_t *channel );
void db_flush_nick( IRCServer_t *server, char *nick, IRCMsgType_t type, 
                    char *text, char *newNick );
bool db_check_nick_notify( IRCChannel_t *channel, char *nick, int hours );
void db_notify_nick( IRCChannel_t *channel, char *nick );
IRCChannel_t *FindChannelNum( IRCServer_t *server, int channum );
void botCmd_initialize( void );
void botCmd_add( char *command, BotCmdFunc_t func );
void botCmd_parse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg );
void botCmd_remove( char *command );

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
