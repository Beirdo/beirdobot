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
#include <pcre.h>
#include "botnet.h"
#include "structs.h"
#include "protos.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdHelp( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
void botCmdList( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg );
char *botHelpHelp( void );
char *botHelpList( void );

static char *botCmdDepthFirst( BalancedBTreeItem_t *item );
void regexpBotCmdParse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                        char *msg, IRCMsgType_t type, int *ovector, 
                        int ovecsize );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static BotCmd_t botCmd[] _UNUSED_ = {
    { "help",       botCmdHelp,   botHelpHelp },
    { "list",       botCmdList,   botHelpList }
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

void regexpBotCmdAdd( IRCServer_t *server, IRCChannel_t *channel )
{
    char           *chanRegexp;
    char           *nickRegexp;

    chanRegexp = (char *)malloc(256);
    if( !chanRegexp ) {
        return;
    }

    nickRegexp = (char *)malloc(256);
    if( !nickRegexp ) {
        free( chanRegexp );
        return;
    }
    
    snprintf( chanRegexp, 256, "(?i)^%s$", channel->fullspec );
    snprintf( nickRegexp, 256, "(?i)^\\s*%s[:,]?\\s*(.*)$", server->nick );

    regexp_add( (const char *)chanRegexp, (const char *)nickRegexp, 
                regexpBotCmdParse );
}

void regexpBotCmdParse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                        char *msg, IRCMsgType_t type, int *ovector, 
                        int ovecsize )
{
    char               *message;

    pcre_get_substring( msg, ovector, ovecsize, 1, (const char **)&message );
    botCmd_parse( server, channel, who, message );
    pcre_free_substring( message );
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

    /* Strip leading spaces */
    while( line && *line == ' ' ) {
        line++;
    }

    /* Strip trailing spaces */
    if( line ) {
        for( len = strlen(line); len && line[len-1] == ' '; 
             len = strlen(line) ) {
            line[len-1] = '\0';
        }

        if( *line == '\0' ) {
            line = NULL;
        }
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
            LoggedChannelMessage(server, channel, helpMsg);
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

    BalancedBTreeLock( botCmdTree );
    message = botCmdDepthFirst( botCmdTree->root );
    BalancedBTreeUnlock( botCmdTree );

    if( server ) {
        if( !channel ) {
            /* Private message */
            BN_SendPrivateMessage(&server->ircInfo, (const char *)who, message);
        } else {
            /* in channel */
            LoggedChannelMessage(server, channel, message);
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


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
