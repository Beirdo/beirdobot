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

/*HEADER---------------------------------------------------
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

#define BOT_NICKNAME "Mega"
#define BOT_USERNAME "MegaBot"
#define BOT_REALNAME "Talk to me"

#define MY_PASS "botnet"

#define DCC_WAITING_FOR_PASS 1
#define DCC_PASS_ACCEPTED    2

int Val;

void ProcOnDCCSendRequest(BN_PInfo I,BN_PSend Send)
{
  BN_AcceptDCCSend(I,Send,PROCESS_KEEP_SOCKET|PROCESS_NEW_THREAD);
}

void ProcOnDCCChatRequest(BN_PInfo I,BN_PChat Chat)
{
  printf("Event Chat Request : %s @ %s\n",Chat->Nick,Chat->Addr);
  BN_AcceptDCCChat(I,Chat,PROCESS_NEW_THREAD);
}

void ProcOnDCCChatOpened(BN_PInfo I,BN_PChat Chat)
{
  printf("Event Chat Opened : %s @ %s\n",Chat->Nick,Chat->Addr);
  Chat->User = (void *)DCC_WAITING_FOR_PASS;
  BN_SendDCCChatMessage(I,Chat,"Enter your password.\n");
}

void ProcOnDCCChatClosed(BN_PInfo I,BN_PChat Chat)
{
  printf("Event Chat Closed : %s @ %s\n",Chat->Nick,Chat->Addr);
}

void ProcOnDCCTalk(BN_PInfo I,BN_PChat Chat,const char Msg[])
{
  printf("Event Chat Talk : %s @ %s : %s\n",Chat->Nick,Chat->Addr,Msg);
  if(Chat->User == (void *)DCC_WAITING_FOR_PASS)
  {
    if(strcmp(Msg,MY_PASS) != 0)
    {
      BN_SendDCCChatMessage(I,Chat,"Negative on that, Houston. Bye\n");
      BN_CloseDCCChat(I,Chat);
    }
    else
      Chat->User = (void *)DCC_PASS_ACCEPTED;
  }
  else if(Chat->User == (void *)DCC_PASS_ACCEPTED)
  {
    printf("DCC CHAT MESSAGE RECEIVED FROM VALID USER : %s\n",Msg);
    Val = atoi(Msg);
    BN_SendPrivateMessage(I,"ze_killer","Hi there, how do you feel right now ?");
  }
  else
    printf("Bad value in OnDCCTalk : %p\n",Chat->User);
}


void ProcOnConnected(BN_PInfo I,const char HostName[])
{
  printf("Event Connected : (%s)\n",HostName);
  BN_EnableFloodProtection(I,100,1000,60);
  BN_Register(I,BOT_NICKNAME,BOT_USERNAME,BOT_REALNAME);
}

void ProcOnPingPong(BN_PInfo I)
{
  printf("Event PingPong\n");
}

void ProcOnStatus(BN_PInfo I,const char Msg[],int Code)
{
  printf("Event Status : (%s)\n",Msg);
}

void ProcOnRegistered(BN_PInfo I)
{
  printf("Event Registered\n");
  //BN_SendJoinMessage(I,"#toto",NULL);
//  BN_SendMessage(I,BN_MakeMessage(NULL,"MODE","#toto"),BN_LOW_PRIORITY);
//  BN_SendMessage(I,BN_MakeMessage(NULL,"LIST",""),BN_LOW_PRIORITY);
  Val = 666; // Modify the variable, main's Val will only be modified if same thread
}

void ProcOnUnknown(BN_PInfo I,const char Who[],const char Command[],const char Msg[])
{
  printf("Unknown event from %s : %s %s\n",Who,Command,Msg);
}

void ProcOnError(BN_PInfo I,int err)
{
  printf("Event Error : (%d)\n",err);
}

void ProcOnDisconnected(BN_PInfo I,const char Msg[])
{
  printf("Event Disconnected : (%s)\n",Msg);
}

void ProcOnNotice(BN_PInfo I,const char Who[],const char Whom[],const char Msg[])
{
  printf("You (%s) have have notice by %s (%s)\n",Whom,Who,Msg);
}

char *ProcOnCTCP(BN_PInfo I,const char Who[],const char Whom[],const char Type[])
{
  char *S;

  printf("You (%s) have received a CTCP request from %s (%s)\n",Whom,Who,Type);
  S = malloc(sizeof("Forget about it")+1);
  strcpy(S,"Forget about it");
  return S;
}

void ProcOnCTCPReply(BN_PInfo I,const char Who[],const char Whom[],const char Msg[])
{
  printf("%s has replied to your (%s) CTCP request (%s)\n",Who,Whom,Msg);
}

void ProcOnWhois(BN_PInfo I,const char *Chans[])
{
  int i;

  printf("Whois Infos:\n");
  for(i=0;i<WHOIS_INFO_COUNT;i++)
    printf("\t(%s)\n",Chans[i]);
  printf("End of list\n");
}

void ProcOnMode(BN_PInfo I,const char Channel[],const char Who[],const char Msg[])
{
  printf("Mode for %s by %s : %s\n",Channel,Who,Msg);
}

void ProcOnModeIs(BN_PInfo I,const char Channel[],const char Msg[])
{
  printf("Mode for %s : %s\n",Channel,Msg);
}

void ProcOnNames(BN_PInfo I,const char Channel[],const char *Names[],int Count)
{
  int i;
  printf("Names for channel (%s) :\n",Channel);
  for(i=0;i<Count;i++)
    printf("\t(%s)\n",Names[i]);
  printf("End of names for (%s)\n",Channel);
  BN_SendMessage(I,BN_MakeMessage(NULL,"WHO","#toto"),BN_LOW_PRIORITY);
}

void ProcOnLinks(BN_PInfo I,const char Server[],const char *Links[],int Count)
{
  int i;
  printf("Links for %s :\n",Server);
  for(i=0;i<Count;i++)
    printf("\t%s\n",Links[i]);
  printf("End of names for (%s)\n",Server);
}

void ProcOnWho(BN_PInfo I,const char Channel[],const char *Info[],const int Count)
{
  int i;

  printf("Who infos for channel (%s)\n",Channel);
  for(i=0;i<(Count*WHO_INFO_COUNT);i+=WHO_INFO_COUNT)
    printf("\t%s,%s,%s,%s,%s,%s\n",Info[i+0],Info[i+1],Info[i+2],Info[i+3],Info[i+4],Info[i+5]);
  printf("End of Who for (%s)\n",Channel);
}

void ProcOnBanList(BN_PInfo I,const char Channel[],const char *BanList[],const int Count)
{
  int i;

  printf("Ban list for channel %s\n",Channel);
  for(i=0;i<Count;i++)
    printf("\t%s\n",BanList[i]);
  printf("End of ban list for %s\n",Channel);
}

void ProcOnList(BN_PInfo I,const char *Channels[],const char *Counts[],const char *Topics[],const int Count)
{
  int i;

  for(i=0;i<Count;i++)
    printf("%s (%s) : %s\n",Channels[i],Counts[i],Topics[i]);
}

void ProcOnKill(BN_PInfo I,const char Who[],const char Whom[],const char Msg[])
{
  printf("%s has been killed by %s (%s)\n",Whom,Who,Msg);
}

void ProcOnInvite(BN_PInfo I,const char Chan[],const char Who[],const char Whom[])
{
  printf("You (%s) have been invited to %s by %s\n",Whom,Chan,Who);
}

void ProcOnTopic(BN_PInfo I,const char Chan[],const char Who[],const char Msg[])
{
  printf("Topic for %s has been changed by %s (%s)\n",Chan,Who,Msg);
}

void ProcOnKick(BN_PInfo I,const char Chan[],const char Who[],const char Whom[],const char Msg[])
{
  printf("%s has been kicked from %s by %s (%s)\n",Whom,Chan,Who,Msg);
}

void ProcOnPrivateTalk(BN_PInfo I,const char Who[],const char Whom[],const char Msg[])
{
  printf("%s sent you (%s) a private message (%s)\n",Who,Whom,Msg);
}

void ProcOnAction(BN_PInfo I,const char Chan[],const char Who[],const char Msg[])
{
  printf("%s sent an action to %s : %s\n",Who,Chan,Msg);
}

void bot_start(void)
{
  BN_TInfo Info;
#ifdef _WIN32
  DWORD Res;
#elif __unix__
  pid_t Res;
#endif

  char *server = NULL;
  uint16 port = 6667;

  memset(&Info,0,sizeof(Info));
  Info.CB.OnConnected = ProcOnConnected;
  Info.CB.OnPingPong = ProcOnPingPong;
  Info.CB.OnRegistered = ProcOnRegistered;
  Info.CB.OnUnknown = ProcOnUnknown;
  Info.CB.OnDisconnected = ProcOnDisconnected;
  Info.CB.OnError = ProcOnError;
  Info.CB.OnNotice = ProcOnNotice;
  Info.CB.OnStatus = ProcOnStatus;
  Info.CB.OnCTCP = ProcOnCTCP;
  Info.CB.OnCTCPReply = ProcOnCTCPReply;
  Info.CB.OnWhois = ProcOnWhois;
  Info.CB.OnMode = ProcOnMode;
  Info.CB.OnModeIs = ProcOnModeIs;
  Info.CB.OnNames = ProcOnNames;
  Info.CB.OnLinks = ProcOnLinks;
  Info.CB.OnWho = ProcOnWho;
  Info.CB.OnBanList = ProcOnBanList;
  Info.CB.OnList = ProcOnList;
  Info.CB.OnKill = ProcOnKill;
  Info.CB.OnInvite = ProcOnInvite;
  Info.CB.OnTopic = ProcOnTopic;
  Info.CB.OnKick = ProcOnKick;
  Info.CB.OnPrivateTalk = ProcOnPrivateTalk;
  Info.CB.OnAction = ProcOnAction;

  Info.CB.Chat.OnDCCChatRequest = ProcOnDCCChatRequest;
  Info.CB.Chat.OnDCCChatOpened = ProcOnDCCChatOpened;
  Info.CB.Chat.OnDCCChatClosed = ProcOnDCCChatClosed;
  Info.CB.Chat.OnDCCTalk = ProcOnDCCTalk;

  Info.CB.Send.OnDCCSendRequest = ProcOnDCCSendRequest;

  Val = 0; // Val initialized to 0 here... If new PROCESS, cannot change this variable... otherwise we can

#ifdef DEBUG
  while(BN_Connect(&Info,server,port,0) != true)
#else
  while(BN_Connect(&Info,server,port,PROCESS_NEW_THREAD) != true)
#endif
  {
    printf("Disconnected.\n");
#ifdef __unix__
    sleep(10);
#elif _WIN32
    Sleep(10*1000);
#endif
    printf("Reconnecting...\n");
  }

#ifdef __unix__
  do
  {
    Res = waitpid(-1,NULL,WNOHANG);
    printf("Val = %d\n",Val);
    sleep(5);
  } while(Res <= 0);
#elif _WIN32
  do
  {
    GetExitCodeThread(Info.Thread,&Res);
    printf("Val = %d\n",Val);
    Sleep(5*1000);
  } while(Res == STILL_ACTIVE);
#endif
}

