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


/**
 * @file
 * @brief Logs messages to the console and/or logfiles
 */

/* INCLUDE FILES */
#include "environment.h"
#include "botnet.h"
#define _LogLevelNames_
#include "logging.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "protos.h"
#include "queue.h"
#include "linked_list.h"

/* INTERNAL CONSTANT DEFINITIONS */

/* INTERNAL TYPE DEFINITIONS */
typedef struct
{
    LogLevel_t          level;
    pthread_t           threadId;
    char               *file;
    int                 line;
    char               *function;
    struct timeval      tv;
    char               *message;
} LoggingItem_t;


/* INTERNAL MACRO DEFINITIONS */
#define LOGLINE_MAX 256

/* INTERNAL FUNCTION PROTOTYPES */
void *LoggingThread( void *arg );
void LogWrite( LogFileChain_t *logfile, char *text, int length );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

LogLevel_t LogLevel = LOG_UNKNOWN;  /**< The log level mask to apply, messages
                                         must be at at least this priority to
                                         be output */
QueueObject_t  *LoggingQ;
LinkedList_t   *LogList;
pthread_t       loggingThreadId;

/**
 * @brief Formats and enqueues a log message for the Logging thread
 * @param level the logging level to log at
 * @param file the sourcefile the message is from
 * @param line the line in the sourcefile the message is from
 * @param function the function the message is from
 * @param format the printf-style format string
 *
 * Creates a log message (up to LOGLINE_MAX) in length using vsnprintf, and
 * enqueues it with a timestamp, thread and sourcefile info.  These messages 
 * go onto the LoggingQ which is then read by the Logging thread.  When this
 * function returns, all strings passed in can be reused or freed.
 */
void LogPrintLine( LogLevel_t level, char *file, int line, char *function,
                   char *format, ... )
{
    LoggingItem_t *item;
    va_list arguments;

    item = (LoggingItem_t *)malloc(sizeof(LoggingItem_t));
    if( !item ) {
        return;
    }

    if( item->level > LogLevel ) {
        return;
    }

    item->level     = level;
    item->threadId  = pthread_self();
    item->file      = file;
    item->line      = line;
    item->function  = function;
    gettimeofday( &item->tv, NULL );
    item->message   = (char *)malloc(LOGLINE_MAX+1);
    if( !item->message ) {
        free( item );
        return;
    }

    va_start(arguments, format);
    vsnprintf(item->message, LOGLINE_MAX, format, arguments);
    va_end(arguments);

    QueueEnqueueItem( LoggingQ, item );
}

void logging_initialize( void )
{
    LoggingQ = QueueCreate(1024);
    LogList = LinkedListCreate();

    LogStdoutAdd();
    LogSyslogAdd( LOG_LOCAL7 );

    pthread_create( &loggingThreadId, NULL, LoggingThread, NULL );
}
    

/**
 * @brief Prints the log messages to the console (and logfile)
 * @param arg unused
 * @return never returns until shutdown
 * @todo Add support for a logfile as well as console output.
 *
 * Dequeues log messages from the LoggingQ and outputs them to the console.
 * If the message's log level is lower (higher numerically) than the current
 * system log level, the message will be dumped and not displayed.
 * In the future, it will also log to a logfile.
 */
void *LoggingThread( void *arg )
{
    LoggingItem_t      *item;
    struct tm           ts;
    char                line[MAX_STRING_LENGTH];
    char                usPart[9];
    char                timestamp[TIMESTAMP_MAX];
    int                 length;
    LinkedListItem_t   *listItem, *next;
    LogFileChain_t     *logFile;

    LogPrintNoArg( LOG_NOTICE, "Started LoggingThread" );

    while( 1 ) {
        item = (LoggingItem_t *)QueueDequeueItem( LoggingQ, -1 );
        if( !item ) {
            continue;
        }

        localtime_r( (const time_t *)&(item->tv.tv_sec), &ts );
        strftime( timestamp, TIMESTAMP_MAX-8, "%Y-%b-%d %H:%M:%S",
                  (const struct tm *)&ts );
        snprintf( usPart, 9, ".%06d ", (int)(item->tv.tv_usec) );
        strcat( timestamp, usPart );
        length = strlen( timestamp );
        
        LinkedListLock( LogList );
        
        for( listItem = LogList->head; listItem; listItem = next ) {
            logFile = (LogFileChain_t *)listItem;
            next = listItem->next;

            switch( logFile->type ) {
            case LT_SYSLOG:
                syslog( item->level, "%s", item->message );
                break;
            case LT_CONSOLE:
                sprintf( line, "%s %s\n", timestamp, item->message );
                LogWrite( logFile, line, strlen(line) );
                break;
            case LT_FILE:
                sprintf( line, "%s %s:%d (%s) - %s\n", timestamp, item->file,
                         item->line, item->function, item->message );
                LogWrite( logFile, line, strlen(line) );
                break;
            default:
                break;
            }

            if( logFile->aborted ) {
                LogOutputRemove( logFile );
            }
        }

        LinkedListUnlock( LogList );

        free( item->message );
        free( item );
    }

    return( NULL );
}

bool LogStdoutAdd( void )
{
    /* STDOUT corresponds to file descriptor 1 */
    if( Daemon ) {
        return( FALSE );
    }

    LogOutputAdd( 1, LT_CONSOLE, NULL );
    return( TRUE );
}


bool LogSyslogAdd( int facility )
{
    openlog( "beirdobot", LOG_NDELAY | LOG_PID, facility );
    LogOutputAdd( -1, LT_SYSLOG, NULL );
    return( TRUE );
}


void LogOutputAdd( int fd, LogFileType_t type, void *identifier )
{
    LogFileChain_t *item;

    item = (LogFileChain_t *)malloc(sizeof(LogFileChain_t));
    memset( item, 0, sizeof(LogFileChain_t) );

    item->type    = type;
    item->aborted = FALSE;
    switch( type )
    {
        case LT_SYSLOG:
            item->fd = -1;
            break;
        case LT_FILE:
            item->fd = fd;
            item->identifier.filename = strdup( (char *)identifier );
            break;
        case LT_CONSOLE:
            item->fd = fd;
            break;
        default:
            /* UNKNOWN! */
            free( item );
            return;
            break;
    }

    /* Add it to the Log File List (note, the function contains the mutex
     * handling
     */
    LinkedListAdd( LogList, (LinkedListItem_t *)item, UNLOCKED, AT_TAIL );
}


bool LogOutputRemove( LogFileChain_t *logfile )
{
    if( logfile == NULL )
    {
        return( FALSE );
    }

    /* logfile will be pointing at the offending member, close then 
     * remove it.  It is assumed that the caller already has the Mutex
     * locked.
     */
    switch( logfile->type )
    {
        case LT_FILE:
        case LT_CONSOLE:
            close( logfile->fd );
            if( logfile->identifier.filename != NULL )
            {
                free( logfile->identifier.filename );
            }
            break;
        case LT_SYSLOG:
            /* Nothing to do */
            break;
        default:
            break;
    }

    /* Remove the log file from the linked list */
    LinkedListRemove( LogList, (LinkedListItem_t *)logfile, LOCKED );

    free( logfile );
    return( TRUE );
}

void LogWrite( LogFileChain_t *logfile, char *text, int length )
{
    int result;

    if( logfile->aborted == FALSE )
    {
        result = write( logfile->fd, text, length );
        if( result == -1 )
        {
            LogPrint( LOG_UNKNOWN, "Closed Log output on fd %d due to errors", 
                      logfile->fd );
            logfile->aborted = TRUE;
        }
    }
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
