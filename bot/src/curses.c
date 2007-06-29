/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2007 Gavin Hurlbut
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
* Copyright 2007 Gavin Hurlbut
* All rights reserved
*
*/

#include "environment.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <ncurses.h>
#include "botnet.h"
#include "protos.h"
#include "queue.h"
#include "linked_list.h"
#include "logging.h"


static char ident[] _UNUSED_= 
    "$Id$";

QueueObject_t  *CursesQ;
QueueObject_t  *CursesLogQ;
pthread_t       cursesInThreadId;
pthread_t       cursesOutThreadId;

WINDOW         *winFull;
WINDOW         *winHeader;
WINDOW         *winMenu1;
WINDOW         *winMenu2;
WINDOW         *winDetails;
WINDOW         *winLog;
WINDOW         *winTailer;

void *curses_output_thread(void *arg);
void *curses_input_thread(void *arg);
void cursesWindowSet(void);

typedef enum {
    CURSES_TEXT_ADD,
    CURSES_TEXT_REMOVE,
    CURSES_WINDOW_CLEAR,
    CURSES_LOG_MESSAGE,
    CURSES_MENU_ITEM,
    CURSES_KEYSTROKE,
    CURSES_SIGNAL
} CursesType_t;

typedef enum {
    WINDOW_HEADER,
    WINDOW_MENU1,
    WINDOW_MENU2,
    WINDOW_DETAILS,
    WINDOW_LOG,
    WINDOW_TAILER,
    WINDOW_COUNT
} CursesWindow_t;

WINDOW        **windows[] = {
    &winHeader,
    &winMenu1,
    &winMenu2,
    &winDetails,
    &winLog,
    &winTailer
};

typedef struct {
    LinkedListItem_t    linkage;
    CursesWindow_t      window;
    int                 x;
    int                 y;
    char               *string;
    int                 len;
} CursesText_t;

typedef struct {
    char               *message;
} CursesLog_t;

typedef void (*CursesMenuFunc_t)(int, void *);
typedef struct {
    int                 menuId;
    int                 level;
    CursesMenuFunc_t    menuFunc;
    char               *string;
    bool                add;
} CursesMenuItem_t;

typedef struct {
    int             keystroke;
} CursesKeystroke_t;

typedef struct {
    CursesType_t    type;
    union {
        CursesText_t        text;
        CursesLog_t         log;
        CursesMenuItem_t    menuItem;
        CursesKeystroke_t   key;
    } data;
} CursesItem_t;

LinkedList_t       *textEntries[WINDOW_COUNT];

void cursesWindowSet(void)
{
    int         lines;

    lines = (LINES-4)/2;

    initscr();
    winFull    = newwin( 0, 0, 0, 0 );
    winHeader  = subwin( winFull, 2, COLS, 0, 0 );
    winMenu1   = subwin( winFull, lines, 15, 2, 0 );
    winMenu2   = subwin( winFull, lines, (COLS / 2) - 15, 2, 15 );
    winDetails = subwin( winFull, lines, COLS / 2, 2, COLS / 2 );
    winLog     = subwin( winFull, lines, COLS, lines + 2, 0 );
    winTailer  = subwin( winFull, 2, COLS, (2 * lines) + 2, 0 );

    curs_set(0);
    nodelay(stdscr, TRUE);
    cbreak();
    noecho();
}

void curses_start(void)
{
    int         i;

    for( i = 0; i < WINDOW_COUNT; i++ ) {
        textEntries[i] = LinkedListCreate();
    }

    cursesWindowSet();

    CursesQ    = QueueCreate(2048);
    CursesLogQ = QueueCreate(2048);

    thread_create( &cursesOutThreadId, curses_output_thread, NULL, 
                   "thread_curses_out", NULL, NULL );
    thread_create( &cursesInThreadId, curses_input_thread, NULL, 
                   "thread_curses_in", NULL, NULL );
}

void *curses_output_thread(void *arg)
{
    CursesItem_t       *item;
    CursesWindow_t      winNum;
    CursesText_t       *textItem;
    CursesLog_t        *logItem;
    LinkedListItem_t   *listItem;
    bool                found;
    bool                scrolledBack;
    int                 count;
    int                 logTop;
    int                 lines;
    int                 logEntry;
    int                 i;

    scrolledBack = FALSE;

    LogPrintNoArg( LOG_NOTICE, "Starting curses output thread" );

    while( !GlobalAbort ) {
        item = (CursesItem_t *)QueueDequeueItem( CursesQ, 1000 );
        if( !item ) {
            /* Refresh the screen */
            goto UpdateScreen;
        }

        switch( item->type ) {
        case CURSES_TEXT_ADD:
            winNum = item->data.text.window;

            if( winNum >= 0 && winNum <= WINDOW_COUNT ) {
                textItem = (CursesText_t *)malloc(sizeof(CursesText_t));
                memcpy( textItem, &item->data.text, sizeof(CursesText_t) );
                LinkedListAdd( textEntries[winNum], 
                               (LinkedListItem_t *)textItem, AT_TAIL, 
                               UNLOCKED );
            }
            break;
        case CURSES_TEXT_REMOVE:
            winNum = item->data.text.window;

            if( winNum >= 0 && winNum <= WINDOW_COUNT ) {
                LinkedListLock( textEntries[winNum] );
                for( listItem = textEntries[winNum]->head, found = FALSE;
                     listItem && !found; listItem = listItem->next ) {
                    textItem = (CursesText_t *)listItem;
                    if( textItem->x == item->data.text.x &&
                        textItem->y == item->data.text.y ) {
                        found = TRUE;
                        LinkedListRemove( textEntries[winNum], listItem,
                                          LOCKED );
                    }
                }
                LinkedListUnlock( textEntries[winNum] );
            }
            break;
        case CURSES_WINDOW_CLEAR:
            winNum = item->data.text.window;

            if( winNum >= 0 && winNum <= WINDOW_COUNT ) {
                LinkedListLock( textEntries[winNum] );
                while( (listItem = textEntries[winNum]->head) ) {
                    textItem = (CursesText_t *)listItem;
                    LinkedListRemove( textEntries[winNum], listItem, LOCKED );
                    free( textItem->string );
                    free( textItem );
                }
                LinkedListUnlock( textEntries[winNum] );
            }
            break;
        case CURSES_LOG_MESSAGE:
            count = 0;
            while( QueueUsed( CursesLogQ ) >= 2000 ) {
                logItem = (CursesLog_t *)QueueDequeueItem( CursesLogQ, 0 );
                if( logItem ) {
                    free( logItem->message );
                    free( logItem );
                }
                count++;
            }

            logItem = (CursesLog_t *)malloc(sizeof(CursesLog_t));
            memcpy( logItem, &item->data.log, sizeof(CursesLog_t) );
            QueueEnqueueItem( CursesLogQ, (void *)logItem );

            if( scrolledBack ) {
                QueueLock( CursesLogQ );

                if( ((logTop + count) & CursesLogQ->numMask) == 
                    CursesLogQ->tail ) {
                    logTop = CursesLogQ->tail;
                }

                QueueUnlock( CursesLogQ );
            }
            break;
        case CURSES_MENU_ITEM:
            break;
        case CURSES_KEYSTROKE:
            LogPrint( LOG_CRIT, "Keystroke: %d", item->data.key.keystroke );
            break;
        case CURSES_SIGNAL:

            break;
        default:
            break;
        }

        free( item );

    UpdateScreen:
        for( i = 0; i < WINDOW_COUNT; i++ ) {
            LinkedListLock( textEntries[i] );

            for( listItem = textEntries[i]->head; listItem; 
                 listItem = listItem->next ) {
                textItem = (CursesText_t *)listItem;

                mvwprintw( *windows[i], textItem->y, textItem->x, "%s",
                           textItem->string );
            }
            LinkedListUnlock( textEntries[i] );

            /* sync updates to the full window */
            wsyncup( *windows[i] );
        }

#if 0
    UpdateLogs:
#endif
        QueueLock( CursesLogQ );
        lines = (LINES-4)/2;

        count = QueueUsed( CursesLogQ );
        if( count > lines ) {
            if( !scrolledBack ) {
                logTop = (CursesLogQ->head + CursesLogQ->numElements - lines) &
                         CursesLogQ->numMask;
            }
        } else {
            logTop = CursesLogQ->tail;
        }

        for( i = 0; i < count; i++ ) {
            logEntry = (logTop + i) & CursesLogQ->numMask;
            logItem = (CursesLog_t *)CursesLogQ->itemTable[logEntry];
            mvwprintw( winLog, i, 0, "%s", logItem->message );
        }

        QueueUnlock( CursesLogQ );

        /* sync updates to the full window */
        wsyncup( winLog );
        wrefresh( winFull );
    }

    LogPrintNoArg( LOG_NOTICE, "Ending curses output thread" );
    return(NULL);
}

void *curses_input_thread(void *arg)
{
    int                 ch;
    CursesItem_t       *item;

    LogPrintNoArg( LOG_NOTICE, "Starting curses input thread" );

    while( !GlobalAbort ) {
        ch = wgetch( winFull );
        if( ch == ERR ) {
            /* No character, timed out */
            continue;
        }

        item = (CursesItem_t *)malloc(sizeof(CursesItem_t));
        item->type = CURSES_KEYSTROKE;
        item->data.key.keystroke = ch;
        QueueEnqueueItem( CursesQ, (void *)item );
    }

    LogPrintNoArg( LOG_NOTICE, "Ending curses input thread" );
    return(NULL);
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
