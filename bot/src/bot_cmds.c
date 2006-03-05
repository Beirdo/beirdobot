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

/* INCLUDE FILES */
#include "environment.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdHelp( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdList( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who,
                   char *msg );
void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg );
char *botHelpHelp( void );
char *botHelpList( void );
char *botHelpSeen( void );
char *botHelpNotice( void );

static char *botCmdDepthFirst( BalancedBTreeItem_t *item );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static BotCmd_t botCmd[] _UNUSED_ = {
    { "help",       botCmdHelp,   botHelpHelp },
    { "list",       botCmdList,   botHelpList },
    { "search",     botCmdSearch, NULL },
    { "seen",       botCmdSeen,   botHelpSeen },
    { "notice",     botCmdNotice, botHelpNotice }
};
static int botCmdCount _UNUSED_ = NELEMENTS(botCmd);

BalancedBTree_t    *botCmdTree;


void botCmd_initialize( void )
{
    int                     i;
    BalancedBTreeItem_t    *item;

    botCmdTree = BalancedBTreeCreate( BTREE_KEY_STRING );
    BalancedBTreeLock( botCmdTree );

    for( i = 0; i < botCmdCount; i++ ) {
        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( item ) {
            item->item = (void *)&botCmd[i];
            item->key  = (void *)&botCmd[i].command;
            BalancedBTreeAdd( botCmdTree, item, LOCKED, FALSE );
        }
    }

    BalancedBTreeAdd( botCmdTree, NULL, LOCKED, TRUE );
    BalancedBTreeUnlock( botCmdTree );
}

void botCmd_add( const char **command, BotCmdFunc_t func, 
                 BotCmdHelpFunc_t helpFunc )
{
    BalancedBTreeItem_t    *item;
    BotCmd_t               *cmd;

    if( !func ) {
        return;
    }

    item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
    cmd = (BotCmd_t *)malloc(sizeof(BotCmd_t));
    if( !item ) {
        return;
    }

    if( !cmd ) {
        free( item );
        return;
    }

    cmd->command  = (char *)*command;
    cmd->func     = func;
    cmd->helpFunc = helpFunc;
    item->item = (void *)cmd;
    item->key  = (void *)command;
    BalancedBTreeAdd( botCmdTree, item, UNLOCKED, TRUE );
}

void botCmd_remove( char *command )
{
    BalancedBTreeItem_t    *item;

    BalancedBTreeLock( botCmdTree );
    item = BalancedBTreeFind( botCmdTree, (void *)&command, LOCKED );

    if( item ) {
        BalancedBTreeRemove( botCmdTree, item, LOCKED, TRUE );
        free( item->item );
    }
    BalancedBTreeUnlock( botCmdTree );
}

int botCmd_parse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                  char *msg )
{
    char                   *line;
    char                   *cmd;
    int                     len;
    BalancedBTreeItem_t    *item;
    BotCmd_t               *cmdStruct;
    int                     ret;

    line = strstr( msg, " " );
    if( line ) {
        /* Command has trailing text, skip the space */
        len = line - msg;
        line++;

        cmd = (char *)malloc( len + 2 );
        strncpy( cmd, msg, len );
        cmd[len] = '\0';
    } else {
        /* Command is the whole line */
        cmd = strdup( msg );
    }

    ret = 0;
    item = BalancedBTreeFind( botCmdTree, (void *)&cmd, UNLOCKED );
    if( item ) {
        cmdStruct = (BotCmd_t *)item->item;
        cmdStruct->func( server, channel, who, line );
        ret = 1;
    }
    free( cmd );

    return( ret );
}


void botCmdHelp( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg )
{
    char                   *line;
    char                   *cmd;
    int                     len;
    BalancedBTreeItem_t    *item;
    BotCmd_t               *cmdStruct;
    static char            *help = "help";
    static char            *helpNotFound = "Currently no help on this subject";
    char                   *helpMsg;

    if( !msg ) {
        msg = help;
    }

    printf( "Bot CMD: Help %s by %s\n", msg, who );

    line = strstr( msg, " " );
    if( line ) {
        /* Command has trailing text, skip the space */
        len = line - msg;
        line++;

        cmd = (char *)malloc( len + 2 );
        strncpy( cmd, msg, len );
        cmd[len] = '\0';
    } else {
        /* Command is the whole line */
        cmd = strdup( msg );
    }

    helpMsg = helpNotFound;

    item = BalancedBTreeFind( botCmdTree, (void *)&cmd, UNLOCKED );
    if( item ) {
        cmdStruct = (BotCmd_t *)item->item;
        if( cmdStruct->helpFunc ) {
            helpMsg = cmdStruct->helpFunc();
        }
    }
    free( cmd );

    if( server ) {
        if( !channel ) {
            /* Private message */
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, helpMsg);
        } else {
            /* in channel */
            BN_SendChannelMessage(&server->ircInfo, 
                                  (const char *)channel->channel, helpMsg);
        }
    } else {
        /* Used for debugging purposes */
        printf( "help: %s\n", helpMsg );
    }
}

char *botHelpHelp( void )
{
    static char *help = "Use \"help\" followed by a bot command.  For a list "
                        "of bot commands, use \"list\"";
    return( help );
}

static char *botCmdDepthFirst( BalancedBTreeItem_t *item )
{
    char       *message;
    char       *oldmsg;
    char       *submsg;
    int         len;

    message = NULL;

    if( !item ) {
        return( message );
    }

    submsg = botCmdDepthFirst( item->left );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    submsg = strdup(*((char **)item->key));
    message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
    if( oldmsg ) {
        strcat( message, ", " );
    } else {
        message[0] = '\0';
    }
    strcat( message, submsg );
    free( submsg );

    submsg = botCmdDepthFirst( item->right );
    if( submsg ) {
        len = strlen( message );

        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        strcat( message, ", " );
        strcat( message, submsg );
        free( submsg );
    }

    return( message );
}

void botCmdList( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg )
{
    char       *message;

    if( !msg ) {
        printf( "Bot CMD: List NULL by %s\n", who );
    } else {
        printf( "Bot CMD: List %s by %s\n", msg, who );
    }

    BalancedBTreeLock( botCmdTree );
    message = botCmdDepthFirst( botCmdTree->root );
    BalancedBTreeUnlock( botCmdTree );

    if( server ) {
        if( !channel ) {
            /* Private message */
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message);
        } else {
            /* in channel */
            BN_SendChannelMessage(&server->ircInfo, 
                                  (const char *)channel->channel, message);
        }
    } else {
        /* Used for debugging purposes */
        printf( "command list: %s\n", message );
    }

    free( message );
}

char *botHelpList( void )
{
    static char *help = "Shows a list of supported bot commands.";

    return( help );
}

void botCmdSearch( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg )
{
    if( !msg ) {
        printf( "Bot CMD: Search NULL by %s\n", who );
    } else {
        printf( "Bot CMD: Search %s by %s\n", msg, who );
    }
}

void botCmdSeen( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg )
{
    char           *message;
    static char    *huh = "Huh? Who?";

    if( !server ) {
        return;
    }

    if( !channel ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                              "This needs to be done in a channel!");
        return;
    }

    if( !msg ) {
        message = huh;
    } else {
        message = db_get_seen( channel, msg );
    }

    BN_SendChannelMessage(&server->ircInfo, 
                          (const char *)channel->channel, message);
    
    if( message != huh ) {
        free( message );
    }
}

char *botHelpSeen( void )
{
    static char *help = "Shows when the last time a user has been seen, or how"
                        " long they've been idle.  Must be done in a channel.";

    return( help );
}

void botCmdNotice( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                   char *msg )
{
    char           *message;

    if( !server ) {
        return;
    }

    if( !channel ) {
        BN_SendPrivateMessage(&server->ircInfo, (const char *)who, 
                              "This needs to be done in a channel!");
        return;
    }

    message = (char *)malloc(MAX_STRING_LENGTH);
    if( strcmp( channel->url, "" ) ) {
        snprintf( message, MAX_STRING_LENGTH, 
                  "This channel (%s) is logged -- %s", channel->channel, 
                  channel->url );
    } else {
        snprintf( message, MAX_STRING_LENGTH,
                  "This channel (%s) has no configured URL for logs",
                  channel->channel );
    }

    BN_SendChannelMessage(&server->ircInfo, 
                          (const char *)channel->channel, message);
    
    free( message );
}

char *botHelpNotice( void )
{
    static char *help = "Shows the channel's notice which includes the URL to "
                        " the logs online.";

    return( help );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
