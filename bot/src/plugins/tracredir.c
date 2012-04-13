/*
* This file is part of the beirdobot package
* Copyright (C) 2012 Raymond Wagner
*
* This plugin uses code gratuitously stolen from the 'fart' plugin.
* See that for any real licensing information.
*/

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2012 Raymond Wagner
* All rights reserved
*/

/* INCLUDE FILES */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botnet.h"
#include "environment.h"
#include "structs.h"
#include "protos.h"
#include "logging.h"

/* INTERNAL FUNCTION PROTOTYPES */
void regexpFuncTrac( IRCServer_t *server, IRCChannel_t *channel, char *who,
                     char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                     void *tag );

/* CVS generated ID string */
static char ident[] _UNUSED_ =
    "$Id$";

static char *contentRegexp = "(?i)(?:\\B|\\s+|^)(?<!build\\s)#(\\d+)(?:\\s|\\.|,|$)";

void plugin_initialize( char *args )
{
    LogPrintNoArg( LOG_NOTICE, "Initializing trac redirect..." );
    regexp_add( NULL, (const char *)contentRegexp, regexpFuncTrac, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing trac redirect..." );
    regexp_remove( NULL, contentRegexp );
}

void regexpFuncTrac( IRCServer_t *server, IRCChannel_t *channel, char *who,
                     char *msg, IRCMsgType_t type, int *ovector, int ovecsize,
                     void *tag )
{
    char *message, *ticketid;

    if( !channel ) {
        return;
    }

    ticketid = regexp_substring( msg, ovector, ovecsize, 1 );
    if (ticketid)
    {
        message = (char *)malloc(35+strlen(ticketid)+2);
        sprintf( message, "http://code.mythtv.org/trac/ticket/%s", ticketid );
        LoggedActionMessage( server, channel, message );
        free(ticketid);
        free(message);
    }
}

/*
* vim:ts=4:sw=4:ai:et:si:sts=4
*/
