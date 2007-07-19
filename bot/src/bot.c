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
 * based on Bot Net Example file 
 * (c) Christophe CALMEJANE - 1999'01 
 * aka Ze KiLleR / SkyTech 
 */

#include "environment.h"
#ifndef __USE_BSD
#define __USE_BSD
#endif
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __unix__
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <errno.h>
#include <signal.h>
#include "botnet.h"
#include "protos.h"
#include "structs.h"
#include "linked_list.h"
#include "balanced_btree.h"
#include "logging.h"


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

BalancedBTree_t    *ServerTree = NULL;
bool                ChannelsLoaded = FALSE;
IRCChannel_t       *newChannel;

void *bot_server_thread(void *arg);
void botSighup( int signum, void *arg );
void serverStartTree( BalancedBTreeItem_t *node );
bool serverKillTree( BalancedBTreeItem_t *node, bool ifVisited );
void cursesServerRevert( void *arg, char *string );
void cursesChannelRevert( void *arg, char *string );
void serverSaveFunc( void *arg, int index, char *string );
void channelSaveFunc( void *arg, int index, char *string );
IRCServer_t *RecurseFindServerWithChannel( BalancedBTreeItem_t *node, 
                                           int channelId );
void cursesServerNew( void *arg );
void cursesServerCleanup( void *arg );
void cursesChannelNew( void *arg );
void cursesChannelCleanup( void *arg );


void ProcOnConnected(BN_PInfo I, const char HostName[])
{
    IRCServer_t *server;

    server = (IRCServer_t *)I->User;
    LogPrint( LOG_NOTICE, "Connected to %s:%d as %s...", server->server, 
              server->port, server->nick);

    if( verbose ) {
        LogPrint( LOG_DEBUG, "Event Connected : (%s)", HostName);
    }

    /* We are doing our own, thanks! */
    BN_DisableFloodProtection(I);

    if( strcmp( server->password, "" ) ) {
        transmitMsg( server, TX_PASSWORD, NULL, server->password );
    }
    transmitMsg( server, TX_NICK, NULL, server->nick );
    transmitMsg( server, TX_REGISTER, server->username, server->realname );
}

void ProcOnStatus(BN_PInfo I, const char Msg[], int Code)
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "Event Status : (%s)", Msg);
    }
}

void ProcOnExcessFlood(BN_PInfo I, const char Msg[])
{
    if( verbose || 1 ) {
        LogPrint( LOG_DEBUG, "Would Excess Flood: (%s)", Msg);
    }
}

void ProcOnRegistered(BN_PInfo I)
{
    bool                found;
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;

    server = (IRCServer_t *)I->User;

    if( verbose ) {
        LogPrintNoArg( LOG_DEBUG, "Event Registered");
    }

    if( strcmp(server->nickserv, "") ) {
        /* We need to register with nickserv */
        transmitMsg( server, TX_PRIVMSG, server->nickserv, 
                     server->nickservmsg );
    }

    if( server->channels ) {
        LinkedListLock( server->channels );
        for( found = false, item = server->channels->head; 
             item && !found; item = item->next ) {
            channel = (IRCChannel_t *)item;
            if( channel->joined || !channel->enabled ) {
                continue;
            }

            transmitMsg( server, TX_JOIN, channel->channel, NULL );
            found = true;
        }
        LinkedListUnlock( server->channels );
    }
}

void ProcOnUnknown(BN_PInfo I, const char Who[], const char Command[],
                   const char Msg[])
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "Unknown event from %s : %s %s", Who, Command, 
                  Msg);
    }
}

void ProcOnError(BN_PInfo I, int err)
{
    IRCServer_t        *server;

    if( verbose || 1 ) {
        server = (IRCServer_t *)I->User;

        LogPrint( LOG_DEBUG, "Event Error : %s (%d) : Server %s", 
                             strerror(err), err, server->server );

#if 0
        do_backtrace( 0, NULL );
#endif
    }
}

void ProcOnDisconnected(BN_PInfo I, const char Msg[])
{
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;

    server = (IRCServer_t *)I->User;

    if( verbose ) {
        LogPrint( LOG_DEBUG, "Event Disconnected : (%s)", Msg);
    }

    if( GlobalAbort ) {
        if( server->channels ) {
            LinkedListLock( server->channels );
            for( item = server->channels->head; item ; item = item->next ) {
                channel = (IRCChannel_t *)item;
                if( channel->enabled ) {
                    db_nick_history( channel, "", HIST_END );
                }
            }
            LinkedListUnlock( server->channels );
        }
        LogPrint( LOG_NOTICE, "Killing thread for %s@%s:%d", server->nick, 
                  server->server, server->port );

        pthread_exit( NULL );
    }
}

void ProcOnNotice(BN_PInfo I, const char Who[], const char Whom[],
                  const char Msg[])
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "You (%s) have notice by %s (%s)\n", Whom, Who,
                  Msg);
    }
}

char *ProcOnCTCP(BN_PInfo I, const char Who[], const char Whom[],
                 const char Type[])
{
    char           *S;

    if( verbose ) {
        LogPrint( LOG_DEBUG, "You (%s) have received a CTCP request from %s "
                             "(%s)", Whom, Who, Type);
    }

    S = NULL;

    if( !strcasecmp(Type, "version") ) {
        S = (char *)malloc(MAX_STRING_LENGTH);
        sprintf( S, "beirdobot -- %s", svn_version() );
    }
    return S;
}

void ProcOnCTCPReply(BN_PInfo I, const char Who[], const char Whom[],
                     const char Msg[])
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "%s has replied to your (%s) CTCP request (%s)", 
                  Who, Whom,
               Msg);
    }
}

void ProcOnWhois(BN_PInfo I, const char *Chans[])
{
    int             i;

    if( verbose ) {
        LogPrintNoArg( LOG_DEBUG, "Whois Infos:");
        for (i = 0; i < WHOIS_INFO_COUNT; i++) {
            LogPrint( LOG_DEBUG, "\t(%s)", Chans[i]);
        }
        LogPrintNoArg( LOG_DEBUG, "End of list");
    }
}

void ProcOnMode(BN_PInfo I, const char Channel[], const char Who[],
                const char Msg[])
{
    char           *string;
    IRCChannel_t   *channel;

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "Mode for %s by %s : %s\n", Channel, Who, Msg);

    channel = FindChannel((IRCServer_t *)I->User, Channel);
    db_add_logentry( channel, (char *)Who, TYPE_MODE, string, true );
    db_update_nick( channel, (char *)Who, true, true );
    free( string );
}

void ProcOnModeIs(BN_PInfo I, const char Channel[], const char Msg[])
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "Mode for %s : %s", Channel, Msg);
    }
}

void ProcOnNames(BN_PInfo I, const char Channel[], const char *Names[],
                 int Count)
{
    int             i;
    IRCServer_t    *server;

    server = (IRCServer_t *)I->User;

    if( verbose ) {
        LogPrint( LOG_DEBUG, "Names for channel (%s) :", Channel);
        for (i = 0; i < Count; i++) {
            LogPrint( LOG_DEBUG, "\t(%s)", Names[i]);
        }
        LogPrint( LOG_DEBUG, "End of names for (%s)", Channel);
    }

    transmitMsg( server, TX_WHO, (char *)Channel, NULL );
}


void ProcOnWho(BN_PInfo I, const char Channel[], const char *Info[],
               const int Count)
{
    int             i;
    IRCChannel_t   *channel;
    IRCServer_t    *server;
    char           *nick;

    server = (IRCServer_t *)I->User;
    channel = FindChannel(server, Channel);

    if( !channel ) {
        LogPrint( LOG_NOTICE, "Channel %s on %s not found in ProcOnWho!",
                              Channel, server->server );
        return;
    }

    if( verbose ) {
        LogPrint( LOG_DEBUG, "Who infos for channel (%s)", Channel);
    }

    db_nick_history( channel, "", HIST_START );
    for (i = 0; i < (Count * WHO_INFO_COUNT); i += WHO_INFO_COUNT) {
        if( verbose ) {
            LogPrint( LOG_DEBUG, "\t%s,%s,%s,%s,%s,%s", Info[i + 0], 
                      Info[i + 1], Info[i + 2], Info[i + 3], Info[i + 4],
                      Info[i + 5]);
        }

        nick = (char *)Info[i + 0];
        db_update_nick( channel, nick, true, false );
        db_nick_history( channel, nick, HIST_INITIAL );
        if( strcmp( channel->url, "" ) && 
            db_check_nick_notify( channel, nick, channel->notifywindow ) ) {
            send_notice( channel, nick );
        }
    }

    if( verbose ) {
        LogPrint( LOG_DEBUG, "End of Who for (%s)", Channel);
    }
}

void ProcOnBanList(BN_PInfo I, const char Channel[], const char *BanList[],
                   const int Count)
{
    int             i;

    if( verbose ) {
        LogPrint( LOG_DEBUG, "Ban list for channel %s", Channel);
        for (i = 0; i < Count; i++) {
            LogPrint( LOG_DEBUG, "\t%s", BanList[i]);
        }
        LogPrint( LOG_DEBUG, "End of ban list for %s", Channel);
    }
}

void ProcOnList(BN_PInfo I, const char *Channels[], const char *Counts[],
                const char *Topics[], const int Count)
{
    int             i;

    if( verbose ) {
        for (i = 0; i < Count; i++) {
            LogPrint( LOG_DEBUG, "%s (%s) : %s", Channels[i], Counts[i], 
                      Topics[i]);
        }
    }
}

void ProcOnKill(BN_PInfo I, const char Who[], const char Whom[],
                const char Msg[])
{
    if( verbose ) {
        LogPrint( LOG_DEBUG, "%s has been killed by %s (%s)", Whom, Who, Msg);
    }
}

void ProcOnInvite(BN_PInfo I, const char Chan[], const char Who[],
                  const char Whom[])
{
    IRCChannel_t   *channel;
    IRCServer_t    *server;

    if( verbose ) {
        LogPrint( LOG_DEBUG, "You (%s) have been invited to %s by %s", Whom, 
                  Chan, Who);
    }

    server = (IRCServer_t *)I->User;
    channel = FindChannel(server, Chan);
    if( !channel || !channel->enabled ) {
        return;
    }

    /*
     * We are configured for this channel, rejoin
     */
    LogPrint( LOG_NOTICE, "Invited to channel %s on server %s by %s", 
              channel->channel, server->server, Who);
    channel->joined = FALSE;
    transmitMsg( server, TX_JOIN, channel->channel, NULL );
}

void ProcOnTopic(BN_PInfo I, const char Chan[], const char Who[],
                 const char Msg[])
{
    char           *string;
    IRCChannel_t   *channel;

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "%s changes topic to %s\n", Who, Msg);

    channel = FindChannel((IRCServer_t *)I->User, Chan);
    db_add_logentry( channel, (char *)Who, TYPE_TOPIC, string, true );
    db_update_nick( channel, (char *)Who, true, true );
    free( string );
}

void ProcOnKick(BN_PInfo I, const char Chan[], const char Who[],
                const char Whom[], const char Msg[])
{
    char           *string;
    IRCChannel_t   *channel;
    IRCServer_t    *server;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Whom, nick, 256);
    sprintf(string, "%s has been kicked from %s by %s (%s)\n", Whom, Chan, Who,
                    Msg);

    server = (IRCServer_t *)I->User;
    channel = FindChannel(server, Chan);
    db_add_logentry( channel, (char *)Who, TYPE_KICK, string, true );
    db_update_nick( channel, nick, false, false );
    db_nick_history( channel, nick, HIST_LEAVE );
    free( string );

    if( !strcasecmp( Whom, server->nick ) ) {
#ifdef REJOIN_ON_KICK
        /*
         * We just got kicked.  The NERVE!  Join again.
         */
        channel->joined = FALSE;
        transmitMsg( server, TX_JOIN, channel->channel, NULL );
#endif
        LogPrint( LOG_NOTICE, "Kicked from channel %s on server %s by %s (%s)", 
                  channel->channel, server->server, Who, Msg);
    }
}

void ProcOnPrivateTalk(BN_PInfo I, const char Who[], const char Whom[],
                       const char Msg[])
{
    char            nick[256];

    if( verbose ) {
        LogPrint( LOG_DEBUG, "%s sent you (%s) a private message (%s)", Who, 
                  Whom, Msg);
    }
    BN_ExtractNick(Who, nick, 256);
    botCmd_parse( (IRCServer_t *)I->User, NULL, nick, (char *)Msg );
}

void ProcOnAction(BN_PInfo I, const char Chan[], const char Who[],
                  const char Msg[])
{
    IRCServer_t    *server;
    IRCChannel_t   *channel;
    char            nick[256];

    server  = (IRCServer_t *)I->User;
    channel = FindChannel(server, Chan);
    db_add_logentry( channel, (char *)Who, TYPE_ACTION, (char *)Msg, true );
    db_update_nick( channel, (char *)Who, true, true );

    BN_ExtractNick(Who, nick, 256);
    regexp_parse( server, channel, nick, (char *)Msg, TYPE_ACTION );
}

void ProcOnChannelTalk(BN_PInfo I, const char Chan[], const char Who[],
                       const char Msg[])
{
    IRCChannel_t   *channel;
    IRCServer_t    *server;
    char            nick[256];
    int             ret;

    server  = (IRCServer_t *)I->User;
    channel = FindChannel(server, Chan);
    db_add_logentry( channel, (char *)Who, TYPE_MESSAGE, (char *)Msg, true );
    db_update_nick( channel, (char *)Who, true, true );

    BN_ExtractNick(Who, nick, 256);

    ret = 0;
    if( channel->cmdChar ) {
        if( Msg[0] == channel->cmdChar ) {
            ret = botCmd_parse( server, channel, nick, (char *)&Msg[1] );
        }
    }

    if( !ret ) {
        /* There was no command match */
        regexp_parse( server, channel, nick, (char *)Msg, TYPE_MESSAGE );
    }
}

void ProcOnNick(BN_PInfo I, const char Who[], const char Msg[])
{
    db_flush_nick( (IRCServer_t *)I->User, (char *)Who, TYPE_NICK, (char *)Msg,
                   (char *)Msg );
}

void ProcOnJoin(BN_PInfo I, const char Chan[], const char Who[])
{
    char           *string;
    IRCChannel_t   *channel;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Who, nick, 256);
    sprintf(string, "%s (%s) has joined %s\n", nick, Who, Chan);

    channel = FindChannel((IRCServer_t *)I->User, Chan);
    db_add_logentry( channel, nick, TYPE_JOIN, string, false );
    db_update_nick( channel, nick, true, false );
    db_nick_history( channel, nick, HIST_JOIN );

    if( strcmp( channel->url, "" ) && 
        db_check_nick_notify( channel, nick, channel->notifywindow ) ) {
        send_notice( channel, nick );
    }
    free( string );
}

void ProcOnPart(BN_PInfo I, const char Chan[], const char Who[], 
                const char Msg[])
{
    char           *string;
    IRCChannel_t   *channel;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Who, nick, 256);
    sprintf(string, "%s (%s) has left %s (%s)\n", nick, Who, Chan, Msg);

    channel = FindChannel((IRCServer_t *)I->User, Chan);
    db_add_logentry( channel, nick, TYPE_PART, string, false );
    db_update_nick( channel, nick, false, false );
    db_nick_history( channel, nick, HIST_LEAVE );
    free( string );
}

void ProcOnQuit(BN_PInfo I, const char Who[], const char Msg[])
{
    char           *string;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Who, nick, 256);
    sprintf(string, "%s (%s) has quit (%s)\n", nick, Who, Msg);
    db_flush_nick( (IRCServer_t *)I->User, (char *)Who, TYPE_QUIT, string, 
                   NULL );
    free( string );
}

void ProcOnJoinChannel(BN_PInfo I, const char Chan[])
{
    bool                found;
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;

    server = (IRCServer_t *)I->User;
    LogPrint( LOG_NOTICE, "Joined channel %s on server %s", Chan, 
              server->server);

    if( server->channels ) {
        LinkedListLock( server->channels );
        for( found = false, item = server->channels->head; 
             item && !found; item = item->next ) {
            channel = (IRCChannel_t *)item;
            if( channel->joined || !channel->enabled ) {
                continue;
            }

            if( !strcasecmp(Chan, channel->channel) ) {
                channel->joined = TRUE;
                channel->newChannel = FALSE;
                db_flush_nicks( channel );
                continue;
            }

            transmitMsg( server, TX_JOIN, channel->channel, NULL );
            found = true;
        }
        LinkedListUnlock( server->channels );
    }
}


void bot_start(void)
{
    /* Create the server tree */
    ServerTree = BalancedBTreeCreate( BTREE_KEY_INT );

    versionAdd( "botnet", BN_GetVersion() );

    BalancedBTreeLock( ServerTree );

    /* Read the list of servers */
    cursesMenuItemAdd( 2, MENU_SERVERS, "New Server", cursesServerNew, NULL );
    db_load_servers();

    /* Read the list of channels */
    cursesMenuItemAdd( 2, MENU_CHANNELS, "New Channel", cursesChannelNew, 
                       NULL );

    db_load_channels();

    ChannelsLoaded = TRUE;

    serverStartTree( ServerTree->root );

    BalancedBTreeUnlock( ServerTree );
}

void serverStartTree( BalancedBTreeItem_t *node )
{
    IRCServer_t      *server;

    if( !node ) {
        return;
    }

    serverStartTree( node->left );

    server = (IRCServer_t *)node->item;

    if( server->enabled ) {
        serverStart( server );
    }

    serverStartTree( node->right );
}

void serverStart( IRCServer_t *server )
{
    server->newServer = FALSE;
    server->txQueue = QueueCreate( 1024 );
    thread_create( &server->txThreadId, transmit_thread, (void *)server, 
                   server->txThreadName, NULL );

    server->callbacks.sighupFunc = botSighup;
    server->callbacks.sighupArg  = (void *)server;
    thread_create( &server->threadId, bot_server_thread, (void *)server, 
                   server->threadName, &server->callbacks );
}

void *bot_shutdown(void *arg)
{
    if( ServerTree ) {
        BalancedBTreeLock( ServerTree );

        while( serverKillTree( ServerTree->root, FALSE ) ) {
            /*
             * This recurses and kills off every entry in the tree, which
             * messes up recursion, so needs restarting
             */
        }

        BalancedBTreeDestroy( ServerTree );
        ServerTree = NULL;
    }

    LogPrintNoArg( LOG_NOTICE, "Shutdown all bot threads" );
    BotDone = true;

    return( NULL );
}


bool serverKillTree( BalancedBTreeItem_t *node, bool ifVisited )
{
    IRCServer_t            *server;

    if( !node ) {
        return( FALSE );
    }

    if( serverKillTree( node->left, ifVisited ) ) {
        return( TRUE );
    }

    server = (IRCServer_t *)node->item;

    if( !ifVisited || server->visited ) {
        serverKill( node, server, TRUE );
        return( TRUE );
    }

    if( serverKillTree( node->right, ifVisited ) ) {
        return( TRUE );
    }

    return( FALSE );
}

void serverKill( BalancedBTreeItem_t *node, IRCServer_t *server, bool unalloc )
{
    LinkedListItem_t       *listItem, *next;
    IRCChannel_t           *channel;
    BalancedBTreeItem_t    *item;

    if( server->enabled || server->enabledChanged ) {
        server->threadAbort = TRUE;
        thread_deregister( server->txThreadId );
        thread_deregister( server->threadId );
        server->threadAbort = FALSE;
    }

    if( unalloc ) {
        BalancedBTreeRemove( node->btree, node, LOCKED, FALSE );
    } else {
        server->enabled = FALSE;
    }

    if( server->txQueue ) {
        /* This *might* leak the contents of any queue entries? */
        QueueClear( server->txQueue, TRUE );
        QueueLock( server->txQueue );
        QueueDestroy( server->txQueue );
        server->txQueue = NULL;
    }

    if( server->channels ) {
        LinkedListLock( server->channels );
        BalancedBTreeLock( server->channelName );
        BalancedBTreeLock( server->channelNum );

        for( listItem = server->channels->head; listItem; listItem = next ) {
            next = listItem->next;
            channel = (IRCChannel_t *)listItem;

            regexpBotCmdRemove( server, channel );
            LinkedListRemove( server->channels, listItem, LOCKED );

            item = BalancedBTreeFind( server->channelName, 
                                      &channel->channel, LOCKED );
            if( item ) {
                BalancedBTreeRemove( server->channelName, item, LOCKED, 
                                     FALSE );
            }

            item = BalancedBTreeFind( server->channelNum, 
                                      &channel->channelId, LOCKED );
            if( item ) {
                BalancedBTreeRemove( server->channelNum, item, LOCKED, 
                                     FALSE );
            }

            ThreadAllNotifyChannel( channel );

            cursesMenuItemRemove( 2, MENU_CHANNELS, channel->menuText );
            free( channel->menuText );
            free( channel->channel );
            free( channel->fullspec );
            free( channel->url );
            free( channel );
        }

        LinkedListDestroy( server->channels );
        BalancedBTreeDestroy( server->channelName );
        BalancedBTreeDestroy( server->channelNum );
        server->channels = NULL;
        server->channelName = NULL;
        server->channelNum = NULL;
    }

    if( unalloc ) {
        free( server->server );
        free( server->password );
        free( server->nick );
        free( server->username );
        free( server->realname );
        free( server->nickserv );
        free( server->nickservmsg );
        free( server->ircInfo.Server );
    }

    LinkedListLock( server->floodList );
    for( listItem = server->floodList->head; listItem; listItem = next ) {
        next = listItem->next;

        LinkedListRemove( server->floodList, listItem, LOCKED );
        free( listItem );
    }
    LinkedListDestroy( server->floodList );
    server->floodList = NULL;

    ThreadAllNotifyServer( server );

    if( unalloc ) {
        cursesMenuItemRemove( 2, MENU_SERVERS, server->menuText );
        free( server->menuText );
        free( server->threadName );
        free( server->txThreadName );
        free( server );
        free( node );
    }
}

void channelLeave( IRCServer_t *server, IRCChannel_t *channel, 
                   char *oldChannel )
{
    transmitMsg( server, TX_PART, oldChannel, 
                 "Leaving due to reconfiguration" );
    db_nick_history( channel, "", HIST_END );
    channel->joined = FALSE;
}


void *bot_server_thread(void *arg)
{
    BN_TInfo           *Info;
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    LinkedListItem_t   *item;
    sigset_t            sigmsk;

    server = (IRCServer_t *)arg;

    if( !server ) {
        return(NULL);
    }

    sigemptyset( &sigmsk );
    pthread_sigmask( SIG_SETMASK, &sigmsk, NULL );

    Info = &server->ircInfo;

    memset(Info, 0, sizeof(BN_TInfo));
    Info->User = (void *)server;
    Info->CB.OnConnected = ProcOnConnected;
    Info->CB.OnJoinChannel = ProcOnJoinChannel;
    Info->CB.OnRegistered = ProcOnRegistered;
    Info->CB.OnUnknown = ProcOnUnknown;
    Info->CB.OnDisconnected = ProcOnDisconnected;
    Info->CB.OnError = ProcOnError;
    Info->CB.OnNotice = ProcOnNotice;
    Info->CB.OnStatus = ProcOnStatus;
    Info->CB.OnCTCP = ProcOnCTCP;
    Info->CB.OnCTCPReply = ProcOnCTCPReply;
    Info->CB.OnWhois = ProcOnWhois;
    Info->CB.OnMode = ProcOnMode;
    Info->CB.OnModeIs = ProcOnModeIs;
    Info->CB.OnNames = ProcOnNames;
    Info->CB.OnWho = ProcOnWho;
    Info->CB.OnBanList = ProcOnBanList;
    Info->CB.OnList = ProcOnList;
    Info->CB.OnKill = ProcOnKill;
    Info->CB.OnInvite = ProcOnInvite;
    Info->CB.OnTopic = ProcOnTopic;
    Info->CB.OnKick = ProcOnKick;
    Info->CB.OnPrivateTalk = ProcOnPrivateTalk;
    Info->CB.OnAction = ProcOnAction;
    Info->CB.OnChannelTalk = ProcOnChannelTalk;
    Info->CB.OnNick = ProcOnNick;
    Info->CB.OnJoin = ProcOnJoin;
    Info->CB.OnPart = ProcOnPart;
    Info->CB.OnQuit = ProcOnQuit;
    Info->CB.OnExcessFlood = ProcOnExcessFlood;

    LogPrint( LOG_NOTICE, "Connecting to %s:%d as %s...", server->server, 
              server->port, server->nick);

    while (BN_Connect(Info, server->server, server->port, 0) != true)
    {
        LogPrint( LOG_NOTICE, "Disconnected from %s:%d as %s.", server->server, 
                  server->port, server->nick);

        if( GlobalAbort || server->threadAbort ) {
            break;
        }

        sleep(10);

        /* Clear the joined flags so we will rejoin on reconnect */
        if( server->channels ) {
            LinkedListLock( server->channels );
            for( item = server->channels->head; item ; item = item->next ) {
                channel = (IRCChannel_t *)item;
                channel->joined = FALSE;
            }
            LinkedListUnlock( server->channels );
        }

        LogPrint( LOG_NOTICE, "Reconnecting to %s:%d as %s...", server->server,
                  server->port, server->nick);
    }

    if( server->channels ) {
        LinkedListLock( server->channels );
        for( item = server->channels->head; item ; item = item->next ) {
            channel = (IRCChannel_t *)item;
            db_nick_history( channel, "", HIST_END );
        }
        LinkedListUnlock( server->channels );
    }

    LogPrint( LOG_NOTICE, "Exiting thread for %s@%s:%d", server->nick,
                          server->server, server->port );
    return(NULL);
}

IRCChannel_t *FindChannel(IRCServer_t *server, const char *channame)
{
    BalancedBTreeItem_t    *item;
    IRCChannel_t           *channel;

    if( !server || !ChannelsLoaded ) {
        return( NULL );
    }

    item = BalancedBTreeFind( server->channelName, (char **)&channame, 
                              UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    channel = (IRCChannel_t *)item->item;
    return( channel );
}

IRCChannel_t *FindChannelNum( IRCServer_t *server, int channum )
{
    BalancedBTreeItem_t    *item;
    IRCChannel_t           *channel;

    if( !server || !ChannelsLoaded ) {
        return( NULL );
    }

    item = BalancedBTreeFind( server->channelNum, (int *)&channum, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    channel = (IRCChannel_t *)item->item;
    return( channel );
}

IRCServer_t *FindServerNum( int serverId )
{
    IRCServer_t            *server;
    BalancedBTreeItem_t    *item;

    if( !ChannelsLoaded ) {
        return( NULL );
    }

    item = BalancedBTreeFind( ServerTree, &serverId, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    server = (IRCServer_t *)item->item;

    return( server );
}

int FindServerWithChannel( int channelId )
{
    IRCServer_t    *server;

    if( !ChannelsLoaded ) {
        return( -1 );
    }

    BalancedBTreeLock( ServerTree );
    server = RecurseFindServerWithChannel( ServerTree->root, channelId );
    BalancedBTreeUnlock( ServerTree );

    if( server ) {
        return( server->serverId );
    }

    return( -1 );
}

IRCServer_t *RecurseFindServerWithChannel( BalancedBTreeItem_t *node, 
                                           int channelId )
{
    IRCServer_t    *server;
    IRCChannel_t   *channel;

    if( !node ) {
        return( NULL );
    }

    server = RecurseFindServerWithChannel( node->left, channelId );
    if( server ) {
        return( server );
    }

    server = (IRCServer_t *)node->item;
    channel = FindChannelNum( server, channelId );
    if( channel ) {
        return( server );
    }

    return( RecurseFindServerWithChannel( node->right, channelId ) );
}

void LoggedChannelMessage( IRCServer_t *server, IRCChannel_t *channel,
                           char *message )
{
    transmitMsg( server, TX_MESSAGE, channel->channel, message );
    db_add_logentry( channel, server->nick, TYPE_MESSAGE, message, false );
    db_update_nick( channel, server->nick, true, false );
}

void LoggedActionMessage( IRCServer_t *server, IRCChannel_t *channel,
                          char *message )
{
    transmitMsg( server, TX_ACTION, channel->channel, message );
    db_add_logentry( channel, server->nick, TYPE_ACTION, message, false );
    db_update_nick( channel, server->nick, true, false );
}

void botSighup( int signum, void *arg )
{
    IRCServer_t            *server;
    LinkedListItem_t       *listItem, *next;
    BalancedBTreeItem_t    *item;
    IRCChannel_t           *channel;
    bool                    newChannel = FALSE;

    server = (IRCServer_t *)arg;
    if( !server ) {
        return;
    }

    /*
     * Check each channel on the server, leave those no longer needed
     */
    if( server->channels ) {
        LinkedListLock( server->channels );
        BalancedBTreeLock( server->channelName );
        BalancedBTreeLock( server->channelNum );

        for( listItem = server->channels->head; listItem; listItem = next ) {
            next = listItem->next;
            channel = (IRCChannel_t *)listItem;

            if( !channel->visited ) {
                channelLeave( server, channel, channel->channel );
                regexpBotCmdRemove( server, channel );
                LinkedListRemove( server->channels, listItem, LOCKED );

                item = BalancedBTreeFind( server->channelName, 
                                          &channel->channel, LOCKED );
                if( item ) {
                    BalancedBTreeRemove( server->channelName, item, LOCKED,
                                         FALSE );
                }

                item = BalancedBTreeFind( server->channelNum,
                                          &channel->channelId, LOCKED );
                if( item ) {
                    BalancedBTreeRemove( server->channelNum, item, LOCKED,
                                         FALSE );
                }

                ThreadAllNotifyChannel( channel );

                cursesMenuItemRemove( 2, MENU_CHANNELS, channel->menuText );
                free( channel->menuText );
                free( channel->channel );
                free( channel->fullspec );
                free( channel->url );
                free( channel );
            } else if( channel->newChannel && channel->enabled && 
                       !channel->joined && !newChannel ) {
                newChannel = TRUE;
                transmitMsg( server, TX_JOIN, channel->channel, NULL );
            } else if( channel->newChannel && !channel->enabled ) {
                channel->newChannel = FALSE;
            }
        }
        BalancedBTreeUnlock( server->channelNum );
        BalancedBTreeUnlock( server->channelName );
        LinkedListUnlock( server->channels );
    }
}



static CursesFormItem_t     serverFormItems[] = {
    { FIELD_LABEL, 0, 0, 0, 0, "Server Number:  %d", 
      OFFSETOF(serverId,IRCServer_t), FA_INTEGER, 0, FT_NONE, { 0 }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 1, 0, 0, "Server:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 1, 32, 1, "%s", OFFSETOF(server,IRCServer_t), FA_STRING,
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 2, 0, 0, "Port:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL, 
      NULL },
    { FIELD_FIELD, 16, 2, 6, 1, "%d", OFFSETOF(port,IRCServer_t), FA_INTEGER, 6,
      FT_INTEGER, { .integerArgs = { 0, 1, 65535 } }, NULL, NULL },
    { FIELD_LABEL, 0, 3, 0, 0, "Password:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_FIELD, 16, 3, 32, 1, "%s", OFFSETOF(password,IRCServer_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 4, 0, 0, "Nick:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL, 
      NULL },
    { FIELD_FIELD, 16, 4, 32, 1, "%s", OFFSETOF(nick,IRCServer_t), FA_STRING, 
      64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 5, 0, 0, "User Name:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_FIELD, 16, 5, 32, 1, "%s", OFFSETOF(username,IRCServer_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 6, 0, 0, "Real Name:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_FIELD, 16, 6, 32, 1, "%s", OFFSETOF(realname,IRCServer_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 7, 0, 0, "Nickserv Nick:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 7, 32, 1, "%s", OFFSETOF(nickserv,IRCServer_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 8, 0, 0, "Nickserv Msg:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 8, 32, 1, "%s", OFFSETOF(nickservmsg,IRCServer_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 9, 0, 0, "Flood Interval:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 9, 20, 1, "%d", OFFSETOF(floodInterval,IRCServer_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 0, 0x7FFFFFFF } }, 
      NULL, NULL },
    { FIELD_LABEL, 0, 10, 0, 0, "Flood Max Time:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 10, 20, 1, "%d", OFFSETOF(floodMaxTime,IRCServer_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 0, 0x7FFFFFFF } }, 
      NULL, NULL },
    { FIELD_LABEL, 0, 11, 0, 0, "Flood Buffer:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 11, 20, 1, "%d", OFFSETOF(floodBuffer,IRCServer_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 0, 0x7FFFFFFF } }, 
      NULL, NULL },
    { FIELD_LABEL, 0, 12, 0, 0, "Flood Max Line:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 12, 20, 1, "%d", OFFSETOF(floodMaxLine,IRCServer_t), 
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 0, 0x7FFFFFFF } }, 
      NULL, NULL },
    { FIELD_LABEL, 0, 13, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_CHECKBOX, 16, 13, 0, 0, "[%c]", OFFSETOF(enabled,IRCServer_t), 
      FA_BOOL, 3, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 2, 14, 0, 0, "Revert", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesServerRevert, (void *)(-1) },
    { FIELD_BUTTON, 10, 14, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 16, 14, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesCancel, NULL }
};
static int serverFormItemCount = NELEMENTS(serverFormItems);


void serverSaveFunc( void *arg, int index, char *string )
{
    IRCServer_t    *server;

    server = (IRCServer_t *)arg;
    if( index == -1 ) {
        if( server->enabled != server->oldEnabled ) {
            server->enabledChanged = TRUE;
        }

        db_update_server( server );
        mainSighup( 0, NULL );
        botSighup( 0, server );
        return;
    }

    cursesSaveOffset( arg, index, serverFormItems, serverFormItemCount,
                      string );
}

void cursesServerDisplay( void *arg )
{
    IRCServer_t    *server;

    server = (IRCServer_t *)arg;
    server->oldEnabled = server->enabled;
    cursesFormDisplay( arg, serverFormItems, serverFormItemCount, 
                       serverSaveFunc );
}

void cursesServerRevert( void *arg, char *string )
{
    IRCServer_t    *server;

    server = (IRCServer_t *)arg;
    server->oldEnabled = server->enabled;
    cursesFormRevert( arg, serverFormItems, serverFormItemCount, 
                      serverSaveFunc );
}

void cursesServerNew( void *arg )
{
    IRCServer_t            *server;
    BalancedBTreeItem_t    *item;

    server = (IRCServer_t *)malloc(sizeof(IRCServer_t));
    memset( server, 0, sizeof(IRCServer_t) );
    server->serverId = -1;

    item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
    item->item = (void *)server;
    item->key  = (void *)&server->serverId;
    BalancedBTreeAdd( ServerTree, item, UNLOCKED, TRUE );

    server->server      = strdup("");
    server->password    = strdup("");
    server->nick        = strdup("");
    server->username    = strdup("");
    server->realname    = strdup("");
    server->nickserv    = strdup("");
    server->nickservmsg = strdup("");

    cursesRegisterCleanupFunc( cursesServerCleanup );
    cursesServerDisplay( server );
}

void cursesServerCleanup( void *arg )
{
    IRCServer_t            *server;
    BalancedBTreeItem_t    *item;
    int                     num;

    num = -1;
    item = BalancedBTreeFind( ServerTree, &num, UNLOCKED );
    if( !item ) {
        return;
    }

    BalancedBTreeRemove( ServerTree, item, UNLOCKED, TRUE );
    server = (IRCServer_t *)item->item;
    free( server->server );
    free( server->password );
    free( server->nick );
    free( server->username );
    free( server->realname );
    free( server->nickserv );
    free( server->nickservmsg );
    if( server->menuText ) {
        free( server->menuText );
    }
    free( server );
    free( item );
}

static CursesFormItem_t     channelFormItems[] = {
    { FIELD_LABEL, 0, 0, 0, 0, "Channel Number: %d", 
      OFFSETOF(channelId,IRCChannel_t), FA_INTEGER, 0, FT_NONE, { 0 }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 1, 0, 0, "Server Number:", -1, FA_NONE, 0, FT_NONE, 
      { 0 }, NULL, NULL },
    { FIELD_FIELD, 16, 1, 20, 1, "%d", OFFSETOF(server,IRCChannel_t), FA_SERVER,
      20, FT_INTEGER, { .integerArgs = { 0, 1, 4000 } }, NULL, NULL },
    { FIELD_LABEL, 0, 2, 0, 0, "Channel:", -1, FA_NONE, 0, FT_NONE, { 0 }, NULL,
      NULL },
    { FIELD_FIELD, 16, 2, 32, 1, "%s", OFFSETOF(channel,IRCChannel_t), 
      FA_STRING, 64, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 3, 0, 0, "Notify URL:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 3, 32, 1, "%s", OFFSETOF(url,IRCChannel_t), FA_STRING,
      255, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 4, 0, 0, "Notify Window:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 4, 20, 1, "%d", OFFSETOF(notifywindow,IRCChannel_t),
      FA_INTEGER, 20, FT_INTEGER, { .integerArgs = { 0, 0, 168 } }, NULL, 
      NULL },
    { FIELD_LABEL, 0, 5, 0, 0, "Command Char:", -1, FA_NONE, 0, FT_NONE, { 0 },
      NULL, NULL },
    { FIELD_FIELD, 16, 5, 1, 1, "%c", OFFSETOF(cmdChar,IRCChannel_t), FA_CHAR,
      1, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_LABEL, 0, 6, 0, 0, "Enabled:", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      NULL, NULL },
    { FIELD_CHECKBOX, 16, 6, 0, 0, "[%c]", OFFSETOF(enabled,IRCChannel_t), 
      FA_BOOL, 3, FT_NONE, { 0 }, NULL, NULL },
    { FIELD_BUTTON, 2, 7, 0, 0, "Revert", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesChannelRevert, (void *)(-1) },
    { FIELD_BUTTON, 10, 7, 0, 0, "Save", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesSave, (void *)(-1) },
    { FIELD_BUTTON, 16, 7, 0, 0, "Cancel", -1, FA_NONE, 0, FT_NONE, { 0 }, 
      cursesCancel, NULL }
};
static int channelFormItemCount = NELEMENTS(channelFormItems);

void channelSaveFunc( void *arg, int index, char *string )
{
    IRCChannel_t       *channel;

    channel = (IRCChannel_t *)arg;
    if( index == -1 ) {
        if( channel->enabled != channel->oldEnabled ) {
            channel->enabledChanged = TRUE;
        }
        db_update_channel( channel );
        mainSighup( 0, NULL );
        if( channel->server != channel->oldServer ) {
            botSighup( 0, channel->oldServer );
            cursesCancel( arg, string );
        }
        botSighup( 0, channel->server );
        return;
    }

    cursesSaveOffset( arg, index, channelFormItems, channelFormItemCount,
                      string );
    channel->modified = TRUE;
}

void cursesChannelRevert( void *arg, char *string )
{
    IRCChannel_t       *channel;

    channel = (IRCChannel_t *)arg;
    channel->oldServer  = channel->server;
    channel->oldEnabled = channel->enabled; 
    cursesFormRevert( arg, channelFormItems, channelFormItemCount, 
                      channelSaveFunc );
}


void cursesChannelDisplay( void *arg )
{
    IRCChannel_t       *channel;

    channel = (IRCChannel_t *)arg;
    channel->oldServer  = channel->server;
    channel->oldEnabled = channel->enabled; 
    cursesFormDisplay( arg, channelFormItems, channelFormItemCount, 
                       channelSaveFunc );
}

void cursesChannelNew( void *arg )
{
    IRCChannel_t           *channel;

    channel = (IRCChannel_t *)malloc(sizeof(IRCChannel_t));
    newChannel = channel;
    memset( channel, 0, sizeof(IRCChannel_t) );
    channel->channelId = -1;

    channel->channel  = strdup("");
    channel->fullspec = strdup("");
    channel->url      = strdup("");

    cursesRegisterCleanupFunc( cursesChannelCleanup );
    cursesChannelDisplay( channel );
}

void cursesChannelCleanup( void *arg )
{
    if( !newChannel ) {
        return;
    }

    free( newChannel->channel );
    free( newChannel->fullspec );
    free( newChannel->url );
    if( newChannel->menuText ) {
        free( newChannel->menuText );
    }
    free( newChannel );
    newChannel = NULL;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
