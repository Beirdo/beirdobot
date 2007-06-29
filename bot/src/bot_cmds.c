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
#include "balanced_btree.h"
#include "logging.h"

/* INTERNAL FUNCTION PROTOTYPES */
void botCmdHelp( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag );
void botCmdList( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag );
char *botHelpHelp( void *tag );
char *botHelpList( void *tag );

void regexpBotCmdParse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                        char *msg, IRCMsgType_t type, int *ovector, 
                        int ovecsize, void *tag );

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
                 BotCmdHelpFunc_t helpFunc, void *tag )
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
    cmd->tag      = tag;
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
        free( item );
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
    snprintf( nickRegexp, 256, "(?i)^\\s*%s[:,]?\\s+(.*)$", server->nick );

    regexp_add( (const char *)chanRegexp, (const char *)nickRegexp, 
                regexpBotCmdParse, NULL );
}

void regexpBotCmdRemove( IRCServer_t *server, IRCChannel_t *channel )
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
    snprintf( nickRegexp, 256, "(?i)^\\s*%s[:,]?\\s+(.*)$", server->nick );

    regexp_remove( chanRegexp, nickRegexp );
}

void regexpBotCmdParse( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                        char *msg, IRCMsgType_t type, int *ovector, 
                        int ovecsize, void *tag )
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
    BalancedBTreeItem_t    *item;
    BotCmd_t               *cmdStruct;
    int                     ret;

    cmd = CommandLineParse( msg, &line );

    ret = 0;
    item = BalancedBTreeFind( botCmdTree, (void *)&cmd, UNLOCKED );
    if( item ) {
        cmdStruct = (BotCmd_t *)item->item;
        cmdStruct->func( server, channel, who, line, cmdStruct->tag );
        ret = 1;
    }
    free( cmd );

    return( ret );
}


void botCmdHelp( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                 char *msg, void *tag )
{
    char                   *line;
    char                   *cmd;
    BalancedBTreeItem_t    *item;
    BotCmd_t               *cmdStruct;
    static char            *help = "help";
    static char            *helpNotFound = "Currently no help on this subject";
    char                   *helpMsg;

    if( !msg ) {
        msg = help;
    }

    cmd = CommandLineParse( msg, &line );

    helpMsg = helpNotFound;

    item = BalancedBTreeFind( botCmdTree, (void *)&cmd, UNLOCKED );
    if( item ) {
        cmdStruct = (BotCmd_t *)item->item;
        if( cmdStruct->helpFunc ) {
            helpMsg = cmdStruct->helpFunc(cmdStruct->tag);
        }
    }
    free( cmd );

    if( server ) {
        if( !channel ) {
            /* Private message */
            transmitMsg( server, TX_PRIVMSG, who, helpMsg );
        } else {
            /* in channel */
            transmitMsg( server, TX_MESSAGE, channel->channel, helpMsg );
        }
    } else {
        /* Used for debugging purposes */
        LogPrint( LOG_DEBUG, "help: %s", helpMsg );
    }
}

char *botHelpHelp( void *tag )
{
    static char *help = "Use \"help\" followed by a bot command.  For a list "
                        "of bot commands, use \"list\"";
    return( help );
}

char *botCmdDepthFirst( BalancedBTreeItem_t *item, bool filterPlugins )
{
    char       *message;
    char       *oldmsg;
    char       *submsg;
    int         len;
    Plugin_t   *plugin;

    message = NULL;

    if( !item ) {
        return( message );
    }

    submsg = botCmdDepthFirst( item->left, filterPlugins );
    message = submsg;
    oldmsg  = message;
    if( message ) {
        len = strlen(message);
    } else {
        len = -2;
    }
    
    plugin = (Plugin_t *)item->item;
    if( !filterPlugins || plugin->loaded ) {
        submsg = strdup(*((char **)item->key));
        message = (char *)realloc(message, len + 2 + strlen(submsg) + 2);
        if( oldmsg ) {
            strcat( message, ", " );
        } else {
            message[0] = '\0';
        }
        strcat( message, submsg );
        free( submsg );
    }

    submsg = botCmdDepthFirst( item->right, filterPlugins );
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
                 char *msg, void *tag )
{
    char       *message;

    BalancedBTreeLock( botCmdTree );
    message = botCmdDepthFirst( botCmdTree->root, false );
    BalancedBTreeUnlock( botCmdTree );

    if( server ) {
        if( !channel ) {
            /* Private message */
            transmitMsg( server, TX_PRIVMSG, who, message );
        } else {
            /* in channel */
            transmitMsg( server, TX_MESSAGE, channel->channel, message );
        }
    } else {
        /* Used for debugging purposes */
        LogPrint( LOG_DEBUG, "command list: %s", message );
    }

    free( message );
}

char *botHelpList( void *tag )
{
    static char *help = "Shows a list of supported bot commands.";

    return( help );
}

char *CommandLineParse( char *msg, char **pLine )
{
    char       *line;
    char       *cmd;
    int         len;

    if( !msg ) {
        *pLine = NULL;
        return( NULL );
    }

    line = strstr( msg, " " );
    if( line ) {
        /* Command has trailing text, skip the space */
        len = line - msg;
        line++;

        cmd = strndup( msg, len );
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
        for( len = strlen(line); len && line[len-1] == ' '; len-- ) {
            line[len-1] = '\0';
        }

        if( *line == '\0' ) {
            line = NULL;
        }
    }

    *pLine = line;
    return( cmd );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
