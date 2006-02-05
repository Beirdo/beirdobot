/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  nuvtools is free software; you can redistribute it and/or modify
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
 * Bot Net Example file 
 * (c) Christophe CALMEJANE - 1999'01 
 * aka Ze KiLleR / SkyTech 
 */

#include "botnet.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef __unix__
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <errno.h>
#include <string.h>
#include "environment.h"
#include "protos.h"
#include "structs.h"


LinkedList_t   *ServerList;

void *bot_server_thread(void *arg);
IRCChannel_t *FindChannel(IRCServer_t *server, const char *channame);


void ProcOnConnected(BN_PInfo I, const char HostName[])
{
    IRCServer_t *server;

    server = (IRCServer_t *)I->User;
    printf("Event Connected : (%s)\n", HostName);
    BN_EnableFloodProtection(I, 100, 1000, 60);
    BN_Register(I, server->nick, server->username, server->realname);
}

void ProcOnPingPong(BN_PInfo I)
{
    printf("Event PingPong\n");
}

void ProcOnStatus(BN_PInfo I, const char Msg[], int Code)
{
    printf("Event Status : (%s)\n", Msg);
}

void ProcOnRegistered(BN_PInfo I)
{
    bool                found;
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;

    server = (IRCServer_t *)I->User;
    printf("Event Registered\n");

    if( strcmp(server->nickserv, "") ) {
        /* We need to register with nickserv */
        BN_SendPrivateMessage(I, server->nickserv, server->nickservmsg);
    }

    if( server->channels ) {
        LinkedListLock( server->channels );
        for( found = false, item = server->channels->head; 
             item && !found; item = item->next ) {
            channel = (IRCChannel_t *)item;
            if( channel->joined ) {
                continue;
            }

            BN_SendJoinMessage(I, channel->channel, NULL);
            found = true;
        }
        LinkedListUnlock( server->channels );
    }
}

void ProcOnUnknown(BN_PInfo I, const char Who[], const char Command[],
                   const char Msg[])
{
    printf("Unknown event from %s : %s %s\n", Who, Command, Msg);
}

void ProcOnError(BN_PInfo I, int err)
{
    printf("Event Error : (%d)\n", err);
}

void ProcOnDisconnected(BN_PInfo I, const char Msg[])
{
    printf("Event Disconnected : (%s)\n", Msg);
}

void ProcOnNotice(BN_PInfo I, const char Who[], const char Whom[],
                  const char Msg[])
{
    printf("You (%s) have have notice by %s (%s)\n", Whom, Who, Msg);
}

char *ProcOnCTCP(BN_PInfo I, const char Who[], const char Whom[],
                 const char Type[])
{
    char           *S;

    printf("You (%s) have received a CTCP request from %s (%s)\n", Whom,
           Who, Type);
    if( !strcasecmp(Type, "version") ) {
        S = (char *)malloc(MAX_STRING_LENGTH);
        sprintf( S, "beirdobot - %s", svn_version() );
    } else {
        S = strdup("Forget about it");
    }
    return S;
}

void ProcOnCTCPReply(BN_PInfo I, const char Who[], const char Whom[],
                     const char Msg[])
{
    printf("%s has replied to your (%s) CTCP request (%s)\n", Who, Whom,
           Msg);
}

void ProcOnWhois(BN_PInfo I, const char *Chans[])
{
    int             i;

    printf("Whois Infos:\n");
    for (i = 0; i < WHOIS_INFO_COUNT; i++)
        printf("\t(%s)\n", Chans[i]);
    printf("End of list\n");
}

void ProcOnMode(BN_PInfo I, const char Channel[], const char Who[],
                const char Msg[])
{
    char           *string;

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "Mode for %s by %s : %s\n", Channel, Who, Msg);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Channel), (char *)Who, 
                     TYPE_MODE, string );
    free( string );
}

void ProcOnModeIs(BN_PInfo I, const char Channel[], const char Msg[])
{
    printf("Mode for %s : %s\n", Channel, Msg);
}

void ProcOnNames(BN_PInfo I, const char Channel[], const char *Names[],
                 int Count)
{
    int             i;
    printf("Names for channel (%s) :\n", Channel);
    for (i = 0; i < Count; i++)
        printf("\t(%s)\n", Names[i]);
    printf("End of names for (%s)\n", Channel);
    BN_SendMessage(I, BN_MakeMessage(NULL, "WHO", Channel),
                   BN_LOW_PRIORITY);
}


void ProcOnWho(BN_PInfo I, const char Channel[], const char *Info[],
               const int Count)
{
    int             i;

    printf("Who infos for channel (%s)\n", Channel);
    for (i = 0; i < (Count * WHO_INFO_COUNT); i += WHO_INFO_COUNT)
        printf("\t%s,%s,%s,%s,%s,%s\n", Info[i + 0], Info[i + 1],
               Info[i + 2], Info[i + 3], Info[i + 4], Info[i + 5]);
    printf("End of Who for (%s)\n", Channel);
}

void ProcOnBanList(BN_PInfo I, const char Channel[], const char *BanList[],
                   const int Count)
{
    int             i;

    printf("Ban list for channel %s\n", Channel);
    for (i = 0; i < Count; i++)
        printf("\t%s\n", BanList[i]);
    printf("End of ban list for %s\n", Channel);
}

void ProcOnList(BN_PInfo I, const char *Channels[], const char *Counts[],
                const char *Topics[], const int Count)
{
    int             i;

    for (i = 0; i < Count; i++)
        printf("%s (%s) : %s\n", Channels[i], Counts[i], Topics[i]);
}

void ProcOnKill(BN_PInfo I, const char Who[], const char Whom[],
                const char Msg[])
{
    printf("%s has been killed by %s (%s)\n", Whom, Who, Msg);
}

void ProcOnInvite(BN_PInfo I, const char Chan[], const char Who[],
                  const char Whom[])
{
    printf("You (%s) have been invited to %s by %s\n", Whom, Chan, Who);
}

void ProcOnTopic(BN_PInfo I, const char Chan[], const char Who[],
                 const char Msg[])
{
    char           *string;

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "Topic for %s has been changed by %s (%s)\n", Chan, Who, 
                    Msg);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_TOPIC, string );
    free( string );
}

void ProcOnKick(BN_PInfo I, const char Chan[], const char Who[],
                const char Whom[], const char Msg[])
{
    char           *string;

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "%s has been kicked from %s by %s (%s)\n", Whom, Chan, Who,
                    Msg);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_KICK, string );
    free( string );
}

void ProcOnPrivateTalk(BN_PInfo I, const char Who[], const char Whom[],
                       const char Msg[])
{
    printf("%s sent you (%s) a private message (%s)\n", Who, Whom, Msg);
}

void ProcOnAction(BN_PInfo I, const char Chan[], const char Who[],
                  const char Msg[])
{
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_ACTION, (char *)Msg );
}

void ProcOnChannelTalk(BN_PInfo I, const char Chan[], const char Who[],
                       const char Msg[])
{
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_MESSAGE, (char *)Msg );
}

#if 0
void ProcOnNick(BN_PInfo I, const char Who[], const char Msg[])
{
    /* Need to find what channel they were in?! */
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_NICK, (char *)Msg );
}
#endif

void ProcOnJoin(BN_PInfo I, const char Chan[], const char Who[])
{
    char           *string;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Who, nick, 256);
    sprintf(string, "%s (%s) has joined %s\n", nick, Who, Chan);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_JOIN, string );
    free( string );
}

void ProcOnPart(BN_PInfo I, const char Chan[], const char Who[], 
                const char Msg[])
{
    char           *string;
    char            nick[256];

    string = (char *)malloc(MAX_STRING_LENGTH);
    BN_ExtractNick(Who, nick, 256);
    sprintf(string, "%s (%s) has left %s (%s)\n", nick, Who, Chan, Msg);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_PART, string );
    free( string );
}

#if 0
void ProcOnQuit(BN_PInfo I, const char Who[], const char Msg[])
{
    char           *string;

    /* Need to find all channels they are on?! */

    string = (char *)malloc(MAX_STRING_LENGTH);
    sprintf(string, "%s (%s) has quit (%s)\n", BN_ExtractNick(Who), Who, Msg);
    db_add_logentry( FindChannel((IRCServer_t *)I->User, Chan), (char *)Who, 
                     TYPE_QUIT, string );
    free( string );
}
#endif

void ProcOnJoinChannel(BN_PInfo I, const char Chan[])
{
    bool                found;
    LinkedListItem_t   *item;
    IRCServer_t        *server;
    IRCChannel_t       *channel;

    server = (IRCServer_t *)I->User;
    printf("Joined channel %s on server %s\n", Chan, server->server);

    if( server->channels ) {
        LinkedListLock( server->channels );
        for( found = false, item = server->channels->head; 
             item && !found; item = item->next ) {
            channel = (IRCChannel_t *)item;
            if( channel->joined ) {
                continue;
            }

            if( !strcasecmp(Chan, channel->channel) ) {
                channel->joined = true;
                continue;
            }

            BN_SendJoinMessage(I, channel->channel, NULL);
            found = true;
        }
        LinkedListUnlock( server->channels );
    }
}


void bot_start(void)
{
    LinkedListItem_t *item;
    IRCServer_t      *server;

    /* Create the server list */
    ServerList = LinkedListCreate();

    /* Read the list of servers */
    db_load_servers();

    /* Read the list of channels */
    db_load_channels();

    LinkedListLock( ServerList );
    for( item = ServerList->head; item; item = item->next ) {
        server = (IRCServer_t *)item;
        pthread_create( &server->threadId, NULL, bot_server_thread, 
                        (void *)server );
    }
    server = (IRCServer_t *)ServerList->head;
    LinkedListUnlock( ServerList );

    /* Wait for one thread to return -- should likely wait for all of em */
    if( server ) {
        pthread_join( server->threadId, NULL );
    }
}

void *bot_server_thread(void *arg)
{
    BN_TInfo           *Info;
    IRCServer_t        *server;

    server = (IRCServer_t *)arg;

    if( !server ) {
        return(NULL);
    }

    Info = &server->ircInfo;

    memset(Info, 0, sizeof(BN_TInfo));
    Info->User = (void *)server;
    Info->CB.OnConnected = ProcOnConnected;
    Info->CB.OnJoinChannel = ProcOnJoinChannel;
    Info->CB.OnPingPong = ProcOnPingPong;
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
#if 0
    Info->CB.OnNick = ProcOnNick;
#endif
    Info->CB.OnJoin = ProcOnJoin;
    Info->CB.OnPart = ProcOnPart;
#if 0
    Info->CB.OnQuit = ProcOnQuit;
#endif

    printf("Connecting to %s:%d...\n", server->server, server->port);

    while (BN_Connect(Info, server->server, server->port, 0) != true)
    {
        printf("Disconnected from %s:%d.\n", server->server, server->port);
        sleep(10);
        printf("Reconnecting to %s:%d...\n", server->server, server->port);
    }

    return(NULL);
}

IRCChannel_t *FindChannel(IRCServer_t *server, const char *channame)
{
    LinkedListItem_t       *item;
    IRCChannel_t           *channel;
    IRCChannel_t           *chanitem;
    bool                    found;

    channel = NULL;
    if( server->channels ) {
        LinkedListLock( server->channels );
        for( found = false, item = server->channels->head; item && !found; 
             item = item->next ) {
            chanitem = (IRCChannel_t *)item;

            if( !strcasecmp(channame, chanitem->channel) ) {
                found = true;
                channel = chanitem;
            }
        }
        LinkedListUnlock( server->channels );
    }

    return( channel );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
