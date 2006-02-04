/* Bot Net Server Example file
  (c) Christophe CALMEJANE - 1999'01
  aka Ze KiLleR / SkyTech
*/

#include "../makelib/botnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define SERV_PASSWORD "mypass"
#define SERV_VERSION "0210"
#define SERV_MYNAME "services.tmp"
#define SERV_MYINFO "Test server"

void ProcOnDCCChatOpened(BN_PInfo I,BN_PChat Chat)
{
  printf("DCC Chat Opened between %s and %s\n",Chat->To,Chat->Nick);
}

void ProcOnDCCChatClosed(BN_PInfo I,BN_PChat Chat)
{
  printf("DCC Chat Closed between %s and %s\n",Chat->To,Chat->Nick);
}

void ProcOnDCCTalk(BN_PInfo I,BN_PChat Chat,const char Msg[])
{
  printf("DCC Chat Talk between %s and %s : %s\n",Chat->To,Chat->Nick,Msg);
}

void ProcOnDCCChatRequest(BN_PInfo I,BN_PChat Chat)
{
  /* Here, we have a DCC Chat incoming... we gotta check to wich user it is for */
  printf("DCC Chat for %s\n",Chat->To);
  if(strcasecmp(Chat->To,"LameUser") == 0)
  {
    /* We can now safely change callbacks to use for THIS chat connection */
    Chat->CB.OnDCCChatOpened = ProcOnDCCChatOpened;
    Chat->CB.OnDCCChatClosed = ProcOnDCCChatClosed;
    Chat->CB.OnDCCTalk = ProcOnDCCTalk;
    BN_AcceptDCCChat(I,Chat,PROCESS_NEW_THREAD);
  }
  else
    BN_RejectDCCChat(Chat);
}

void ProcOnConnected(BN_PInfo I,const char HostName[])
{
  printf("Event Connected : (%s)\n",HostName);
  /* Connected.... now try to register ourselves as a server */
  BN_ServerSendPassMessage(I,SERV_PASSWORD,SERV_VERSION,false,true);
  BN_ServerSendServerMessage(I,SERV_MYNAME,SERV_MYINFO);
}

void ProcOnPingPong(BN_PInfo I)
{
  printf("Event PingPong\n");
}

void ProcOnStatus(BN_PInfo I,const char Msg[],int Code)
{
  printf("Event Status : (%s)\n",Msg);
}

void ProcOnUnknown(BN_PInfo I,const char Who[],const char Command[],const char Msg[])
{
  printf("Event Unknown from %s : %s %s\n",Who,Command,Msg);
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
  printf("Event Notice from (%s) : (%s)\n",Who,Msg);
}

char *ProcOnCTCP(BN_PInfo I,const char Who[],const char Whom[],const char Type[])
{
  char *S;

  printf("Event CTCP (%s) : (%s)\n",Type,Who);
  S = malloc(sizeof("Forget about it")+1);
  strcpy(S,"Forget about it");
  return S;
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

void ProcOnPass(BN_PInfo I,const char Password[],const char Version[],const char Flags[],const char Options[])
{
  /* Here we check if the remote server have the right password */
  printf("Server PASS : %s %s %s %s\n",Password,Version,Flags,Options);
}

void ProcOnServer(BN_PInfo I,const char Server[],const int HopCount,const int Token,const char Info[])
{
  /* And then, after the pass, we check if the remote server have the right name, and get some infos about it */
  printf("Server SERVER : %s %d %d %s\n",Server,HopCount,Token,Info);

  /* Now we can tell remote server about our users */
  BN_ServerSendNickMessage(I,"LameUser",0,"Lame","localhost",I->MyToken,"+i","Lame user");
  /* And to set the channels the user is on : op on #Lamers */
  BN_ServerSendNJoinMessage(I,"#Lamers","@LameUser");
}

void ProcOnForeignServer(BN_PInfo I,const char Server[],const char Foreign[],const int HopCount,const int Token,const char Info[])
{
  /* If another server is connected (or connecting) to the network */
  printf("Server Foreign SERVER : %s %s %d %d %s\n",Server,Foreign,HopCount,Token,Info);
}

void ProcOnNick(BN_PInfo I,const char Nick[],const int HopCount,const char UserName[],const char HostName[],const int ServToken,const char UMode[],const char RealName[])
{
  /* The NICK command sent by servers when a link is just up */
  printf("Server NICK : %s %d %s %s %d %s %s\n",Nick,HopCount,UserName,HostName,ServToken,UMode,RealName);
}

void ProcOnService(BN_PInfo I,const char ServiceName[],const int ServToken,const char Distribution[],const char Type[],const int HopCount,const char Info[])
{
  /* The SERVICE command sent by servers introducing a new service */
  printf("Server SERVICE : %s %d %s %s %d %s\n",ServiceName,ServToken,Distribution,Type,HopCount,Info);
}

void ProcOnSQuit(BN_PInfo I,const char Server[],const char Msg[])
{
  /* When our link is down */
  printf("Server SQUIT : %s %s\n",Server,Msg);
}

void ProcOnForeignSQuit(BN_PInfo I,const char Server[],const char ServLost[],const char Msg[])
{
  /* When the link of a server of the network is down */
  printf("Server Foreign SQUIT : %s %s %s\n",Server,ServLost,Msg);
}

void ProcOnNJoin(BN_PInfo I,const char Channel[],const char *Nicks[],const int Count)
{
  /* The NJOIN command is sent by servers when the link is up */
  int i;
  printf("Server NJOIN : %s\n",Channel);
  for(i=0;i<Count;i++)
    printf("  %s\n",Nicks[i]);
  printf("End of NJOIN\n");
}


int main()
{
  BN_TInfo Info;

  printf("MEGA SERV\n%s\n",BN_GetCopyright());

  memset(&Info,0,sizeof(Info));
  Info.CB.OnConnected = ProcOnConnected;
  Info.CB.OnPingPong = ProcOnPingPong;
  Info.CB.OnUnknown = ProcOnUnknown;
  Info.CB.OnDisconnected = ProcOnDisconnected;
  Info.CB.OnError = ProcOnError;
  Info.CB.OnNotice = ProcOnNotice;
  Info.CB.OnStatus = ProcOnStatus;
  Info.CB.OnCTCP = ProcOnCTCP;
  Info.CB.OnWhois = ProcOnWhois;
  Info.CB.OnMode = ProcOnMode;
  Info.CB.OnModeIs = ProcOnModeIs;
  Info.CB.OnNames = ProcOnNames;
  Info.CB.OnWho = ProcOnWho;
  Info.CB.OnBanList = ProcOnBanList;
  Info.CB.OnList = ProcOnList;

  Info.CB.Chat.OnDCCChatRequest = ProcOnDCCChatRequest;

  /* Server specific infos */
  /* Set server-server ping delay (30 sec) */
  Info.PingDelay = 30;
  /* Callbacks */
  Info.SCB.OnPass = ProcOnPass;
  Info.SCB.OnServer = ProcOnServer;
  Info.SCB.OnForeignServer = ProcOnForeignServer;
  Info.SCB.OnNick = ProcOnNick;
  Info.SCB.OnService = ProcOnService;
  Info.SCB.OnSQuit = ProcOnSQuit;
  Info.SCB.OnForeignSQuit = ProcOnForeignSQuit;
  Info.SCB.OnNJoin = ProcOnNJoin;

  /* Because we are a LEAF server, WE are connecting another serveer */
  while(BN_Connect(&Info,"rigel.flosysteme.fr",6667,0) != true)
  {
    printf("Disconnected.\n");
    sleep(10);
    printf("Reconnecting...\n");
  }

  wait(NULL);
  return 0;
}

