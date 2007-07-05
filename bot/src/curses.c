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
#include <menu.h>
#include <form.h>
#include "botnet.h"
#include "protos.h"
#include "queue.h"
#include "linked_list.h"
#include "balanced_btree.h"
#include "protected_data.h"
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
WINDOW         *winDetailsForm;

FORM           *detailsForm = NULL;
FIELD         **detailsFields = NULL;

LinkedList_t   *textEntries[WINDOW_COUNT];

void *curses_output_thread( void *arg );
void *curses_input_thread( void *arg );
void cursesWindowSet( void );
void cursesMenuInitialize( void );
void cursesMenuRegenerate( void );
void cursesFormRegenerate( void );
int cursesItemCount( BalancedBTree_t *tree );
int cursesItemCountRecurse( BalancedBTreeItem_t *node );
void cursesSubmenuRegenerate( int menuId, BalancedBTree_t *tree );
void cursesItemAdd( ITEM **items, BalancedBTree_t *tree, int start );
int cursesItemAddRecurse( ITEM **items, BalancedBTreeItem_t *node, int start );
void cursesSubmenuAddAll( BalancedBTreeItem_t *node );
void cursesAtExit( void );
void cursesReloadScreen( void );
void cursesWindowClear( CursesWindow_t window );
void cursesUpdateLines( void );

typedef enum {
    CURSES_TEXT_ADD,
    CURSES_TEXT_REMOVE,
    CURSES_WINDOW_CLEAR,
    CURSES_LOG_MESSAGE,
    CURSES_MENU_ITEM_ADD,
    CURSES_MENU_ITEM_REMOVE,
    CURSES_FORM_ITEM_ADD,
    CURSES_FORM_ITEM_REMOVE,
    CURSES_KEYSTROKE,
    CURSES_SIGNAL
} CursesType_t;

typedef struct {
    WINDOW        **window;
    int             startx;
    int             starty;
    int             width;
    int             height;
} CursesWindowDef_t;

CursesWindowDef_t   windows[] = {
    { &winHeader,       0, 0, 0, 0 },
    { &winMenu1,        0, 0, 0, 0 },
    { &winMenu2,        0, 0, 0, 0 },
    { &winDetails,      0, 0, 0, 0 },
    { &winDetailsForm,  0, 0, 0, 0 },
    { &winLog,          0, 0, 0, 0 },
    { &winTailer,       0, 0, 0, 0 }
};

typedef struct {
    LinkedListItem_t    linkage;
    CursesWindow_t      window;
    CursesTextAlign_t   align;
    int                 x;
    int                 y;
    char               *string;
    int                 len;
} CursesText_t;

typedef struct {
    char               *message;
} CursesLog_t;

typedef struct {
    int                 level;
    int                 menuId;
} CursesMenuItemNotify_t;

typedef struct {
    int             keystroke;
} CursesKeystroke_t;

typedef struct {
    CursesType_t    type;
    union {
        CursesText_t            text;
        CursesLog_t             log;
        CursesMenuItemNotify_t  menu;
        CursesKeystroke_t       key;
    } data;
} CursesItem_t;

typedef struct {
    int                 menuId;
    int                 menuParentId;
    char               *string;
    ITEM               *menuItem;
    CursesMenuFunc_t    menuFunc;
    void               *menuFuncArg;
    BalancedBTree_t    *subMenuTree;
} CursesMenuItem_t;

static CursesMenuItem_t menu1Static[] = {
    { 0, -1, "System",   NULL, cursesDoSubMenu, NULL, NULL },
    { 1, -1, "Servers",  NULL, cursesDoSubMenu, NULL, NULL },
    { 2, -1, "Channels", NULL, cursesDoSubMenu, NULL, NULL },
    { 3, -1, "Database", NULL, cursesDoSubMenu, NULL, NULL },
    { 4, -1, "Plugins",  NULL, cursesDoSubMenu, NULL, NULL }
};
static int menu1StaticCount = NELEMENTS(menu1Static);
static BalancedBTree_t *menu1Tree;
static BalancedBTree_t *menu1NumTree;
CursesMenuItem_t *cursesMenu1Find( int menuId );

typedef struct {
    MENU           *menu;
    ITEM          **items;
    int             itemCount;
    bool            posted;
    int             current;
} CursesMenu_t;

typedef enum {
    LINE_HLINE,
    LINE_VLINE,
    LINE_DOWN_TEE,
    LINE_UP_TEE
} CursesLineType_t;

typedef struct {
    CursesLineType_t    type;
    int                 startx;
    int                 starty;
    int                 x;
    int                 y;
} CursesLine_t;

static CursesMenu_t   **menus = NULL;
static int              menusCount = 0;

static ProtectedData_t         *nextMenuId;
static int                      currMenuId = -1;
static CursesKeyhandleFunc_t    currDetailKeyhandler = NULL;
static bool                     inSubMenuFunc = FALSE;

static CursesLine_t      menuLines[] = {
    { LINE_HLINE, 0, 1, 0, 0 },
    { LINE_HLINE, 0, 0, 0, 0 },
    { LINE_HLINE, 0, 0, 0, 0 },
    { LINE_VLINE, 15, 2, 0, 0 },
    { LINE_VLINE, 0, 2, 0, 0 },
    { LINE_DOWN_TEE, 15, 1, 0, 0 },
    { LINE_DOWN_TEE, 0, 2, 0, 0 },
    { LINE_UP_TEE, 15, 0, 0, 0 },
    { LINE_UP_TEE, 0, 0, 0, 0 }
};
static int menuLinesCount = NELEMENTS( menuLines );

typedef struct {
    LinkedListItem_t        linkage;
    CursesFieldType_t       type;
    int                     startx;
    int                     starty;
    int                     width;
    int                     height;
    char                   *string;
    int                     len;
    int                     maxLen;
    FIELDTYPE              *fieldType;
    CursesFieldTypeArgs_t   fieldArgs;
    FIELD                  *field;
} CursesField_t;

LinkedList_t   *formList;

void cursesWindowSet( void )
{
    int         lines;
    int         i;
    int         x, y;

    initscr();
    curs_set(0);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    winFull    = newwin( 0, 0, 0, 0 );
    getmaxyx( winFull, y, x );
    LogPrint( LOG_INFO, "Window size: %d x %d", x, y );

    lines = (y-5)/2;

    /* hlines */
    menuLines[0].startx = 0;
    menuLines[0].starty = 1;
    menuLines[0].x      = x;

    menuLines[1].startx = 0;
    menuLines[1].starty = lines + 2;
    menuLines[1].x      = x;

    menuLines[2].startx = 0;
    menuLines[2].starty = y - 2;
    menuLines[2].x      = x;

    /* vlines */
    menuLines[3].startx = 15;
    menuLines[3].starty = 2;
    menuLines[3].y      = lines;

    menuLines[4].startx = x / 2;
    menuLines[4].starty = 2;
    menuLines[4].y      = lines;

    /* down-facing tees */
    menuLines[5].startx = 15;
    menuLines[5].starty = 1;

    menuLines[6].startx = x / 2;
    menuLines[6].starty = 1;

    /* up-facing tees */
    menuLines[7].startx = 15;
    menuLines[7].starty = lines + 2;

    menuLines[8].startx = x / 2;
    menuLines[8].starty = lines + 2;

    cursesUpdateLines();
    wrefresh( winFull );

    windows[WINDOW_HEADER].startx = 0;
    windows[WINDOW_HEADER].starty = 0;
    windows[WINDOW_HEADER].width  = x;
    windows[WINDOW_HEADER].height = 1;

    windows[WINDOW_MENU1].startx = 0;
    windows[WINDOW_MENU1].starty = 2;
    windows[WINDOW_MENU1].width  = 15;
    windows[WINDOW_MENU1].height = lines;

    windows[WINDOW_MENU2].startx = 16;
    windows[WINDOW_MENU2].starty = 2;
    windows[WINDOW_MENU2].width  = (x / 2) - 16;
    windows[WINDOW_MENU2].height = lines;

    windows[WINDOW_DETAILS].startx = (x / 2) + 1;
    windows[WINDOW_DETAILS].starty = 2;
    windows[WINDOW_DETAILS].width  = (x - (x / 2)) - 1;
    windows[WINDOW_DETAILS].height = lines;

    windows[WINDOW_DETAILS_FORM].startx = (x / 2) + 1;
    windows[WINDOW_DETAILS_FORM].starty = 2;
    windows[WINDOW_DETAILS_FORM].width  = (x - (x / 2)) - 1;
    windows[WINDOW_DETAILS_FORM].height = lines;

    windows[WINDOW_LOG].startx = 0;
    windows[WINDOW_LOG].starty = lines + 3;
    windows[WINDOW_LOG].width  = x;
    windows[WINDOW_LOG].height = lines + ((y-5) & 1);

    windows[WINDOW_TAILER].startx = 0;
    windows[WINDOW_TAILER].starty = y - 1;
    windows[WINDOW_TAILER].width  = x;
    windows[WINDOW_TAILER].height = 1;

    for( i = 0; i < WINDOW_COUNT; i++ ) {
        if( i == WINDOW_DETAILS_FORM ) {
            *windows[i].window = subwin( winDetails, windows[i].height, 
                                         windows[i].width, windows[i].starty,
                                         windows[i].startx );
        } else {
            *windows[i].window = subwin( winFull, windows[i].height, 
                                         windows[i].width, windows[i].starty,
                                         windows[i].startx );
        }
    }

    wrefresh( winFull );
}

void curses_start( void )
{
    int         i;

    if( Daemon ) {
        return;
    }

    for( i = 0; i < WINDOW_COUNT; i++ ) {
        textEntries[i] = LinkedListCreate();
    }

    cursesWindowSet();
    cursesMenuInitialize();

    CursesQ    = QueueCreate(2048);
    CursesLogQ = QueueCreate(2048);
    formList   = LinkedListCreate();

    thread_create( &cursesOutThreadId, curses_output_thread, NULL, 
                   "thread_curses_out", NULL, NULL );
    thread_create( &cursesInThreadId, curses_input_thread, NULL, 
                   "thread_curses_in", NULL, NULL );
}

void *curses_output_thread( void *arg )
{
    CursesItem_t       *item;
    CursesWindow_t      winNum;
    CursesText_t       *textItem;
    CursesLog_t        *logItem;
    LinkedListItem_t   *listItem;
    bool                found;
    bool                scrolledBack;
    int                 count;
    int                 logTop = 0;
    int                 lines;
    int                 logEntry;
    int                 i;
    ITEM               *currItem;
    CursesMenuItem_t   *menuItem;
    int                 mainMenu = -1;
    int                 len;
    char               *string;
    char               *word;
    char               *ch;
    int                 x, y;
    int                 linewidth;
    int                 linelen;

    scrolledBack = FALSE;
    atexit( cursesAtExit );

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
                               (LinkedListItem_t *)textItem, UNLOCKED,
                               AT_TAIL );
            }
            break;
        case CURSES_TEXT_REMOVE:
            winNum = item->data.text.window;

            if( winNum >= 0 && winNum <= WINDOW_COUNT ) {
                werase( *windows[item->data.text.window].window );
                LinkedListLock( textEntries[winNum] );
                for( listItem = textEntries[winNum]->head, found = FALSE;
                     listItem && !found; listItem = listItem->next ) {
                    textItem = (CursesText_t *)listItem;
                    if( textItem->align == item->data.text.align &&
                        textItem->x == item->data.text.x &&
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
                cursesWindowClear( winNum );
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
        case CURSES_MENU_ITEM_ADD:
        case CURSES_MENU_ITEM_REMOVE:
            /* The real work is done in cursesMenuItemAdd */
            cursesMenuRegenerate();
            break;
        case CURSES_FORM_ITEM_ADD:
        case CURSES_FORM_ITEM_REMOVE:
            /* The real work is done in cursesMenuItemAdd */
            cursesFormRegenerate();
            break;
        case CURSES_KEYSTROKE:
            if( !inSubMenuFunc || !currDetailKeyhandler ||
                currDetailKeyhandler( item->data.key.keystroke ) ) {
                switch( item->data.key.keystroke ) {
                case KEY_UP:
                    menu_driver( menus[currMenuId+1]->menu, REQ_UP_ITEM );
                    break;
                case KEY_DOWN:
                    menu_driver( menus[currMenuId+1]->menu, REQ_DOWN_ITEM );
                    break;
                case KEY_PPAGE:
                    QueueLock( CursesLogQ );
                    getmaxyx( winLog, lines, x );
                    count = QueueUsed( CursesLogQ );

                    if( count <= lines ) {
                        scrolledBack = FALSE;
                        QueueUnlock( CursesLogQ );
                        break;
                    }

                    scrolledBack = TRUE;
                    if( scrolledBack ) {
                        count = (logTop + CursesLogQ->numElements - 
                                 CursesLogQ->tail) & CursesLogQ->numMask;
                        if( count < lines ) {
                            logTop = CursesLogQ->tail;
                        } else {
                            logTop = (logTop + CursesLogQ->numElements - 
                                      lines) & CursesLogQ->numMask;
                        }
                    }
                    QueueUnlock( CursesLogQ );
                    break;
                case KEY_NPAGE:
                    if( !scrolledBack ) {
                        break;
                    }

                    QueueLock( CursesLogQ );
                    getmaxyx( winLog, lines, x );
                    count = QueueUsed( CursesLogQ );

                    if( count <= lines ) {
                        scrolledBack = FALSE;
                        QueueUnlock( CursesLogQ );
                        break;
                    }

                    count = (CursesLogQ->head + CursesLogQ->numElements - 
                             logTop) & CursesLogQ->numMask;
                    if( count <= 2 * lines ) {
                        scrolledBack = FALSE;
                    } else {
                        logTop = (logTop + lines) & CursesLogQ->numMask;
                    }
                    QueueUnlock( CursesLogQ );
                    break;
                case 10:    /* Enter */
                case KEY_RIGHT:
                    currItem = current_item( menus[currMenuId+1]->menu );
                    menuItem = (CursesMenuItem_t *)item_userptr(currItem);
                    pos_menu_cursor( menus[currMenuId+1]->menu );
                    if( menuItem->menuFunc ) {
                        if( menuItem->menuFunc != cursesDoSubMenu ) {
                            inSubMenuFunc = TRUE;
                        }
                        menuItem->menuFunc( menuItem->menuFuncArg );
                        if( detailsForm ) {
                            /* 
                             * That was a form, put the cursor at the first
                             * item on the page
                             */
                            form_driver( detailsForm, REQ_SFIRST_FIELD );
                            form_driver( detailsForm, REQ_END_LINE );
                        }
                    }
                    break;
                case 18:    /* Ctrl-R */
                    cursesReloadScreen();
                    break;
                case 27:    /* Escape */
                case KEY_LEFT:
                    if( !inSubMenuFunc ) {
                        if( currMenuId != -1 ) {
                            pos_menu_cursor( menus[currMenuId+1]->menu );
                            cursesDoSubMenu( &mainMenu );
                        }
                    } else {
                        inSubMenuFunc = FALSE;
                        currDetailKeyhandler = NULL;
                        cursesWindowClear( WINDOW_DETAILS );
                        cursesFormClear();
                        curs_set(0);
                    }
                    break;
                default:
                    break;
                }
            }
            break;
        case CURSES_SIGNAL:
            cursesReloadScreen();
            break;
        default:
            break;
        }

        free( item );

    UpdateScreen:
        cursesUpdateLines();

        for( i = 0; i < WINDOW_COUNT; i++ ) {
            LinkedListLock( textEntries[i] );

            for( listItem = textEntries[i]->head; listItem; 
                 listItem = listItem->next ) {
                textItem = (CursesText_t *)listItem;

                switch( textItem->align ) {
                case ALIGN_LEFT:
                    mvwaddnstr( *windows[i].window, textItem->y, textItem->x, 
                                textItem->string, textItem->len );
                    break;
                case ALIGN_RIGHT:
                    mvwaddnstr( *windows[i].window, textItem->y, 
                                windows[i].width - textItem->x - textItem->len, 
                                textItem->string, textItem->len );
                    break;
                case ALIGN_CENTER:
                    mvwaddnstr( *windows[i].window, textItem->y, 
                                ((windows[i].width - textItem->len) / 2) + 
                                textItem->x, textItem->string, textItem->len );
                    break;
                case ALIGN_FROM_CENTER:
                    mvwaddnstr( *windows[i].window, textItem->y, 
                                (windows[i].width / 2) + textItem->x, 
                                textItem->string, textItem->len );
                    break;
                case ALIGN_WRAP:
                    string    = textItem->string;
                    len       = textItem->len;
                    y         = textItem->y;
                    linewidth = windows[i].width - textItem->x;


                    linelen = 0;
                    word = string;
                    while( len && *string ) {
                        if( len <= linewidth ) {
                            mvwaddnstr( *windows[i].window, y, textItem->x,
                                        string, len );
                            len = 0;
                            continue;
                        }
                        
                        ch = strpbrk( word, " \t\n\r" );
                        if( !ch ) {
                            if( linelen ) {
                                /*
                                 * We accumulated words
                                 */
                                mvwaddnstr( *windows[i].window, y, textItem->x,
                                            string, linelen );
                                string += linelen + 1;
                                word    = string;
                                len    -= linelen + 1;
                            } else {
                                /* 
                                 * no whitespace left... chop a word in the
                                 * middle and nastily hyphenate it
                                 */
                                mvwaddnstr( *windows[i].window, y, textItem->x,
                                            string, linewidth-2 );
                                mvwaddnstr( *windows[i].window, y, 
                                            linewidth-2, "-", 1 );
                                string += linewidth - 2;
                                word    = string;
                                len    -= linewidth - 2;
                            }
                            y++;
                            continue;
                        }

                        if( ch - string > linewidth ) {
                            if( linelen == 0 ) {
                                /* 
                                 * no whitespace left... chop a word in the
                                 * middle and nastily hyphenate it
                                 */
                                mvwaddnstr( *windows[i].window, y, textItem->x,
                                            string, linewidth-2 );
                                mvwaddnstr( *windows[i].window, y, 
                                            linewidth-2, "-", 1 );
                                string += linewidth - 2;
                                word    = string;
                                len    -= linewidth - 2;
                            } else {
                                mvwaddnstr( *windows[i].window, y, textItem->x,
                                            string, linelen );
                                string += linelen + 1;
                                len    -= linelen + 1;
                                linelen = 0;
                                word    = string;
                            }
                            y++;
                        } else if( *ch == '\n' || *ch == '\r' ) {
                            /* OK, we have whitespace */
                            linelen = ch - string;
                            mvwaddnstr( *windows[i].window, y, textItem->x,
                                        string, linelen );

                            string  = ch + 1;
                            len    -= linelen + 1;
                            word    = string;
                            linelen = 0;
                            y++;
                        } else {
                            /* Either tab or space */
                            linelen = ch - string;
                            word    = ch + 1;
                        }
                    }
                    break;
                }
            }
            LinkedListUnlock( textEntries[i] );

            /* sync updates to the full window */
            wsyncup( *windows[i].window );
        }

#if 0
    UpdateLogs:
#endif
        QueueLock( CursesLogQ );
        getmaxyx( winLog, lines, x );

        count = QueueUsed( CursesLogQ );
        if( count > lines ) {
            if( !scrolledBack ) {
                logTop = (CursesLogQ->head + CursesLogQ->numElements - 
                          lines) & CursesLogQ->numMask;
            }
            count = lines;
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

        if( inSubMenuFunc && detailsForm ) {
            pos_form_cursor( detailsForm );
            wrefresh( winFull );
        }
    }

    LogPrintNoArg( LOG_NOTICE, "Ending curses output thread" );
    cursesAtExit();
    return(NULL);
}

void cursesReloadScreen( void )
{
    int         i;

    /* Unpost and free all menus so we can mess with them */
    for( i = 0; i < menusCount; i++ ) {
        if( menus[i]->menu ) {
            menus[i]->current = item_index( current_item( menus[i]->menu ) );
            if( menus[i]->posted ) {
                unpost_menu( menus[i]->menu );
            }
            free_menu( menus[i]->menu );
            menus[i]->menu = NULL;
        }
    }

    if( detailsForm ) {
        unpost_form( detailsForm );
        free_form( detailsForm );

        for( i = 0; detailsFields && detailsFields[i]; i++ ) {
            free_field( detailsFields[i] );
        }
        free( detailsFields );
        detailsFields = NULL;
        detailsForm = NULL;
    }

    delwin( winTailer );
    delwin( winLog );
    delwin( winDetails );
    delwin( winMenu2 );
    delwin( winMenu1 );
    delwin( winHeader );
    delwin( winFull );
    endwin();
    cursesWindowSet();
    cursesMenuRegenerate();
    cursesFormRegenerate();
}

void cursesAtExit( void )
{
    int         x, y;
    curs_set(1);
    echo();
    nl();
    getmaxyx( winFull, y, x );
    wmove( winFull, y, 0 );
    wdeleteln( winFull );
    wrefresh( winFull );
    endwin();
}

void *curses_input_thread( void *arg )
{
    int                 ch;
    CursesItem_t       *item;

    LogPrintNoArg( LOG_NOTICE, "Starting curses input thread" );

    while( !GlobalAbort ) {
        ch = wgetch( stdscr );
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

int cursesMenuItemAdd( int level, int menuId, char *string, 
                       CursesMenuFunc_t menuFunc, void *menuFuncArg )
{
    CursesMenuItem_t       *menuItem;
    CursesItem_t           *cursesItem;
    BalancedBTreeItem_t    *item;
    BalancedBTree_t        *tree;
    int                    *id;
    int                     retval;

    if( Daemon ) {
        return( -1 );
    }

    if( level == 1 ) {
        menuItem = (CursesMenuItem_t *)malloc(sizeof(CursesMenuItem_t));
        
        ProtectedDataLock( nextMenuId );
        id = (int *)nextMenuId->data;
        menuItem->menuId       = (*id)++;
        ProtectedDataUnlock( nextMenuId );

        menuItem->menuParentId = -1;
        menuItem->string       = strdup( string );
        menuItem->menuItem     = new_item( menuItem->string, "" );
        set_item_userptr( menuItem->menuItem, (void *)menuItem );
        menuItem->menuFunc     = cursesDoSubMenu;
        menuItem->menuFuncArg  = (void *)&menuItem->menuId;
        menuItem->subMenuTree  = BalancedBTreeCreate( BTREE_KEY_STRING );

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        item->item = (void *)menuItem;
        item->key  = (void *)&menuItem->menuId;
        BalancedBTreeAdd( menu1NumTree, item, UNLOCKED, TRUE );

        tree = menu1Tree;
        retval = menuItem->menuId;
    } else {
        menuItem = cursesMenu1Find( menuId );
        if( !menuItem ) {
            return( -1 );
        }

        tree = menuItem->subMenuTree;

        menuItem = (CursesMenuItem_t *)malloc(sizeof(CursesMenuItem_t));
        menuItem->menuId       = -1;
        menuItem->menuParentId = menuId;
        menuItem->string       = strdup( string );
        menuItem->menuFunc     = menuFunc;
        menuItem->menuFuncArg  = menuFuncArg;
        menuItem->menuItem     = new_item( menuItem->string, "" );
        set_item_userptr( menuItem->menuItem, (void *)menuItem );
        menuItem->subMenuTree  = NULL;

        retval = menuId;
    }

    item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
    item->item = (void *)menuItem;
    item->key  = (void *)&menuItem->string;
    BalancedBTreeAdd( tree, item, UNLOCKED, TRUE );

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_MENU_ITEM_ADD;
    cursesItem->data.menu.level  = (level == 1 ? 1 : 2);
    cursesItem->data.menu.menuId = retval;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );

    return( retval );
}

void cursesMenuItemRemove( int level, int menuId, char *string )
{
    CursesMenuItem_t       *menuItem;
    CursesMenuItem_t       *subMenuItem;
    CursesItem_t           *cursesItem;
    BalancedBTreeItem_t    *item;
    int                     mainMenu = -1;

    if( Daemon ) {
        return;
    }

    if( level == 1 ) {
        BalancedBTreeLock( menu1Tree );
        item = BalancedBTreeFind( menu1Tree, &string, LOCKED );
        if( !item ) {
            BalancedBTreeUnlock( menu1Tree );
            return;
        }

        menuItem = (CursesMenuItem_t *)item->item;
        BalancedBTreeRemove( menu1Tree, item, LOCKED, TRUE );
        BalancedBTreeUnlock( menu1Tree );
        free( item );
        free( menuItem->string );
        free_item( menuItem->menuItem );

        BalancedBTreeLock( menu1NumTree );
        item = BalancedBTreeFind( menu1NumTree, &menuItem->menuId, LOCKED );
        if( item ) {
            BalancedBTreeRemove( menu1NumTree, item, LOCKED, TRUE );
            free( item );
        }
        BalancedBTreeUnlock( menu1NumTree );
        
        BalancedBTreeLock( menuItem->subMenuTree );
        while( (item = menuItem->subMenuTree->root) ) {
            subMenuItem = (CursesMenuItem_t *)item->item;
            BalancedBTreeRemove( menuItem->subMenuTree, item, LOCKED, FALSE );
            free( item );

            free( subMenuItem->string );
            free_item( subMenuItem->menuItem );
            free( subMenuItem );
        }
        BalancedBTreeDestroy( menuItem->subMenuTree );

        if( currMenuId == menuItem->menuId ) {
            cursesDoSubMenu( &mainMenu );
        }
        free( menuItem );
    } else {
        menuItem = cursesMenu1Find( menuId );
        if( !menuItem ) {
            return;
        }

        BalancedBTreeLock( menuItem->subMenuTree );
        item = BalancedBTreeFind( menuItem->subMenuTree, &string, LOCKED );
        if( !item ) {
            BalancedBTreeUnlock( menuItem->subMenuTree );
            return;
        }
        BalancedBTreeRemove( menuItem->subMenuTree, item, LOCKED, TRUE );
        BalancedBTreeUnlock( menuItem->subMenuTree );
        subMenuItem = (CursesMenuItem_t *)item->item;
        free( item );

        free( subMenuItem->string );
        free_item( subMenuItem->menuItem );
        free( subMenuItem );
    }

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_MENU_ITEM_REMOVE;
    cursesItem->data.menu.level  = (level == 1 ? 1 : 2);
    cursesItem->data.menu.menuId = menuId;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

CursesMenuItem_t *cursesMenu1Find( int menuId )
{
    BalancedBTreeItem_t    *item;
    CursesMenuItem_t       *menuItem;

    if( menuId < 0 ) {
        return( NULL );
    }

    if( menuId < menu1StaticCount ) {
        return( &menu1Static[menuId] );
    }

    item = BalancedBTreeFind( menu1NumTree, &menuId, UNLOCKED );
    if( !item ) {
        return( NULL );
    }

    menuItem = (CursesMenuItem_t *)item->item;
    return( menuItem );
}

void cursesDoSubMenu( void *arg )
{
    int        *item;
    int         menuId;

    item = (int *)arg;
    if( !item ) {
        return;
    }
    menuId = *item;

    if( menus[menuId+1]->itemCount == 0 ) {
        return;
    }

    if( currMenuId != -1 ) {
        unpost_menu( menus[currMenuId+1]->menu );
        menus[currMenuId+1]->posted = FALSE;
        wrefresh( winFull );
    }
    currMenuId = *item;
    if( !menus[menuId+1]->posted ) {
        menus[menuId+1]->posted = TRUE;
        post_menu( menus[menuId+1]->menu );
        wrefresh( winFull );
    }
}

void cursesMenuInitialize( void )
{
    int             i;

    nextMenuId = ProtectedDataCreate();
    nextMenuId->data = (void *)malloc(sizeof(int));
    *(int *)nextMenuId->data = NELEMENTS(menu1Static);

    for( i = 0; i < menu1StaticCount; i++ ) {
        menu1Static[i].menuItem    = new_item( menu1Static[i].string, "" );
        set_item_userptr( menu1Static[i].menuItem, (void *)&menu1Static[i] );
        menu1Static[i].menuFuncArg = (void *)&menu1Static[i].menuId;
        menu1Static[i].subMenuTree = BalancedBTreeCreate( BTREE_KEY_STRING );
    }

    menu1Tree = BalancedBTreeCreate( BTREE_KEY_STRING );
    menu1NumTree = BalancedBTreeCreate( BTREE_KEY_INT );

    cursesMenuRegenerate();
}

void cursesMenuRegenerate( void )
{
    int             count;
    int             i;
    int             x, y;

    ProtectedDataLock( nextMenuId );
    count = *(int *)nextMenuId->data;
    ProtectedDataUnlock( nextMenuId );

    /* Account for the main menu at 0 */
    count++;

    if( count > menusCount ) {
        menus = (CursesMenu_t **)realloc(menus, count * sizeof(CursesMenu_t *));
        for( i = menusCount; i < count; i++ ) {
            menus[i] = (CursesMenu_t *)malloc(sizeof(CursesMenu_t));
            memset( menus[i], 0x00, sizeof(CursesMenu_t) );
        }
        menusCount = count;
    }

    /* Unpost and free all menus so we can mess with them */
    for( i = 0; i < menusCount; i++ ) {
        if( menus[i]->menu ) {
            menus[i]->current = item_index( current_item( menus[i]->menu ) );
            if( menus[i]->posted ) {
                unpost_menu( menus[i]->menu );
            }
            free_menu( menus[i]->menu );
        }
    }

    /* Regenerate main menu */
    BalancedBTreeLock( menu1Tree );
    menus[0]->itemCount = menu1StaticCount + cursesItemCount( menu1Tree );
    count = menus[0]->itemCount + 1;
    menus[0]->items = (ITEM **)realloc(menus[0]->items, count * sizeof(ITEM *));
    for( i = 0; i < menu1StaticCount; i++ ) {
        menus[0]->items[i] = menu1Static[i].menuItem;

        cursesSubmenuRegenerate( menu1Static[i].menuId, 
                                 menu1Static[i].subMenuTree );
    }

    cursesItemAdd( menus[0]->items, menu1Tree, i );
    cursesSubmenuAddAll( menu1Tree->root );
    BalancedBTreeUnlock( menu1Tree );

    /* Recreate & repost menus */
    for( i = 0; i < menusCount; i++ ) {
        if( menus[i]->items ) {
            menus[i]->menu = new_menu( menus[i]->items );
            if( i == 0 ) {
                getmaxyx( winMenu1, y, x );
                set_menu_format( menus[i]->menu, y, 1 );
                set_menu_win( menus[i]->menu, winMenu1 );
                set_menu_sub( menus[i]->menu, winMenu1 );
                menus[i]->posted = TRUE;
            } else {
                getmaxyx( winMenu2, y, x );
                set_menu_format( menus[i]->menu, y, 1 );
                set_menu_win( menus[i]->menu, winMenu2 );
                set_menu_sub( menus[i]->menu, winMenu2 );
            }

            if( menus[i]->current >= menus[i]->itemCount ) {
                menus[i]->current = menus[i]->itemCount - 1;
            }
            if( menus[i]->current < 0 ) {
                menus[i]->current = 0;
            }
            count = menus[i]->current;
            set_current_item( menus[i]->menu, menus[i]->items[count] );
            if( menus[i]->posted ) {
                post_menu( menus[i]->menu );
            }
        } else {
            menus[i]->posted = FALSE;
        }
    }
}

int cursesItemCount( BalancedBTree_t *tree )
{
    if( !tree ) {
        return( 0 );
    }

    return( cursesItemCountRecurse( tree->root ) );
}

int cursesItemCountRecurse( BalancedBTreeItem_t *node )
{
    int         retval = 0;

    if( !node ) {
        return( 0 );
    }

    retval += cursesItemCountRecurse( node->left );

    retval++;

    retval += cursesItemCountRecurse( node->right );

    return( retval );
}

void cursesSubmenuRegenerate( int menuId, BalancedBTree_t *tree )
{
    CursesMenu_t       *menu;
    int                 count;

    if( menuId > menusCount ) {
        return;
    }

    menu = menus[menuId+1];

    if( !tree ) {
        if( menu->items ) {
            free( menu->items );
        }
        if( menu->menu ) {
            free_menu( menu->menu );
        }
        memset( menu, 0x00, sizeof(CursesMenu_t) );
        return;
    }

    BalancedBTreeLock( tree );
    menu->itemCount = cursesItemCount( tree );
    count = menu->itemCount + 1;
    menu->items = (ITEM **)realloc(menu->items, count * sizeof(ITEM *));
    cursesItemAdd( menu->items, tree, 0 );
    BalancedBTreeUnlock( tree );
}

void cursesItemAdd( ITEM **items, BalancedBTree_t *tree, int start )
{
    if( !tree || !items ) {
        return;
    }

    start = cursesItemAddRecurse( items, tree->root, start );
    items[start] = NULL;
}

int cursesItemAddRecurse( ITEM **items, BalancedBTreeItem_t *node, int start )
{
    CursesMenuItem_t   *item;

    if( !items || !node ) {
        return( start );
    }

    start = cursesItemAddRecurse( items, node->left, start );

    item = (CursesMenuItem_t *)node->item;
    items[start++] = item->menuItem;

    start = cursesItemAddRecurse( items, node->right, start );
    return( start );
}

void cursesSubmenuAddAll( BalancedBTreeItem_t *node )
{
    CursesMenuItem_t   *menuItem;

    if( !node ) {
        return;
    }

    cursesSubmenuAddAll( node->left );

    menuItem = (CursesMenuItem_t *)node->item;
    if( menuItem->menuId < menusCount ) {
        cursesSubmenuRegenerate( menuItem->menuId, menuItem->subMenuTree );
    }
    
    cursesSubmenuAddAll( node->right );
}

void cursesLogWrite( char *message )
{
    CursesItem_t       *cursesItem;

    if( Daemon ) {
        return;
    }

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_LOG_MESSAGE;
    cursesItem->data.log.message = strdup(message);
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesTextAdd( CursesWindow_t window, CursesTextAlign_t align, int x, 
                    int y, char *string )
{
    CursesItem_t       *cursesItem;

    if( Daemon ) {
        return;
    }

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_TEXT_ADD;
    cursesItem->data.text.window = window;
    cursesItem->data.text.align  = align;
    cursesItem->data.text.x      = x;
    cursesItem->data.text.y      = y;
    cursesItem->data.text.string = strdup( string );
    cursesItem->data.text.len    = strlen( string );
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesTextRemove( CursesWindow_t window, CursesTextAlign_t align, int x, 
                       int y )
{
    CursesItem_t       *cursesItem;

    if( Daemon ) {
        return;
    }

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_TEXT_REMOVE;
    cursesItem->data.text.window = window;
    cursesItem->data.text.align  = align;
    cursesItem->data.text.x      = x;
    cursesItem->data.text.y      = y;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesSigwinch( int signum, void *arg )
{
    CursesItem_t       *cursesItem;

    if( Daemon ) {
        return;
    }

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_SIGNAL;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesWindowClear( CursesWindow_t window )
{
    CursesText_t       *textItem;
    LinkedListItem_t   *listItem;

    werase( *windows[window].window );
    LinkedListLock( textEntries[window] );
    while( (listItem = textEntries[window]->head) ) {
        textItem = (CursesText_t *)listItem;
        LinkedListRemove( textEntries[window], listItem, LOCKED );
        free( textItem->string );
        free( textItem );
    }
    LinkedListUnlock( textEntries[window] );
}

void cursesUpdateLines( void )
{
    int         i;

    for( i = 0; i < menuLinesCount; i++ ) {
        switch( menuLines[i].type ) {
        case LINE_HLINE:
            mvwhline( winFull, menuLines[i].starty, menuLines[i].startx,
                      ACS_HLINE, menuLines[i].x );
            break;
        case LINE_VLINE:
            mvwvline( winFull, menuLines[i].starty, menuLines[i].startx,
                      ACS_VLINE, menuLines[i].y );
            break;
        case LINE_DOWN_TEE:
            mvwaddch( winFull, menuLines[i].starty, menuLines[i].startx,
                      ACS_TTEE );
            break;
        case LINE_UP_TEE:
            mvwaddch( winFull, menuLines[i].starty, menuLines[i].startx,
                      ACS_BTEE );
            break;
        }
    }
}

bool cursesDetailsKeyhandle( int ch )
{
    switch( ch ) {
    case KEY_LEFT:
    case KEY_PPAGE:
    case KEY_NPAGE:
    case 18:    /* Ctrl-R */
        return( TRUE );
    case KEY_UP:
        break;
    case KEY_DOWN:
        break;
    case KEY_RIGHT:
    default:
        break;
    }

    return( FALSE );
}

void cursesKeyhandleRegister( CursesKeyhandleFunc_t func )
{
    if( !func ) {
        return;
    }

    currDetailKeyhandler = func;
}

void cursesFieldAdd( CursesFieldType_t type, int startx, int starty, int width,
                     int height, char *string, int maxLen, void *fieldType, 
                     CursesFieldTypeArgs_t *fieldArgs )
{
    CursesItem_t   *cursesItem;
    CursesField_t  *field;

    if( !inSubMenuFunc ) {
        return;
    }

    field = (CursesField_t *)malloc(sizeof(CursesField_t));
    memset( field, 0x00, sizeof(CursesField_t) );
    field->type      = type;
    field->startx    = startx;
    field->starty    = starty;
    field->width     = width;
    field->height    = height;
    field->string    = strdup(string);
    field->len       = strlen(string);
    field->maxLen    = maxLen;
    field->fieldType = (FIELDTYPE *)fieldType;
    if( fieldArgs ) {
        memcpy( &field->fieldArgs, fieldArgs, sizeof(CursesFieldTypeArgs_t));
    }

    LinkedListAdd( formList, (LinkedListItem_t *)field, UNLOCKED, AT_TAIL );

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_ADD;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesFormClear( void )
{
    LinkedListItem_t   *item;
    CursesField_t      *fieldItem;
    CursesItem_t       *cursesItem;
    int                 i;

    if( detailsForm ) {
        unpost_form( detailsForm );
        wsyncup( winDetailsForm );
        wrefresh( winFull );
        free_form( detailsForm );

        for( i = 0; detailsFields && detailsFields[i]; i++ ) {
            free_field( detailsFields[i] );
        }
        free( detailsFields );
        detailsFields = NULL;
        detailsForm = NULL;
    }

    LinkedListLock( formList );

    while( (item = formList->head) ) {
        fieldItem = (CursesField_t *)item;

        LinkedListRemove( formList, item, LOCKED );
        free( fieldItem->string );
        free( fieldItem );
    }

    LinkedListUnlock( formList );


    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_REMOVE;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesFormRegenerate( void )
{
    LinkedListItem_t       *item;
    CursesField_t          *fieldItem;
    int                     count;
    int                     i;
    FIELD                  *field;
    int                     x, y;
    int                     width;

    if( !inSubMenuFunc ) {
        return;
    }

    getmaxyx( winDetails, y, x );

    LinkedListLock( formList );

    for( count = 0, item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        if( fieldItem->type != FIELD_LABEL ) {
            count++;
        }
    }

    if( detailsForm ) {
        unpost_form( detailsForm );
        free_form( detailsForm );
        for( i = 0; detailsFields && (field = detailsFields[i]); i++ ) {
            free_field( detailsFields[i] );
        }
    }
    
    detailsFields = (FIELD **)realloc( detailsFields, 
                                       (count + 1) * sizeof(FIELD *) );
    
    for( i = 0, item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        switch( fieldItem->type ) {
        case FIELD_LABEL:
            break;
        case FIELD_FIELD:
            width = fieldItem->width;
            if( width + fieldItem->startx > x - 1 ) {
                width = x - fieldItem->startx - 1;
            }
            fieldItem->field = new_field( fieldItem->height, width,
                                          fieldItem->starty, fieldItem->startx,
                                          0, 0 );
            if( fieldItem->fieldType == TYPE_ALNUM ||
                fieldItem->fieldType == TYPE_ALPHA ) {
                set_field_type( fieldItem->field, fieldItem->fieldType,
                                fieldItem->fieldArgs.minLen );
            } else if( fieldItem->fieldType == TYPE_ENUM ) {
                set_field_type( fieldItem->field, fieldItem->fieldType,
                                fieldItem->fieldArgs.enumArgs.stringList,
                                fieldItem->fieldArgs.enumArgs.caseSensitive,
                                fieldItem->fieldArgs.enumArgs.partialMatch );
            } else if( fieldItem->fieldType == TYPE_INTEGER ) {
                set_field_type( fieldItem->field, fieldItem->fieldType,
                                fieldItem->fieldArgs.integerArgs.precision,
                                fieldItem->fieldArgs.integerArgs.minValue,
                                fieldItem->fieldArgs.integerArgs.maxValue );
            } else if( fieldItem->fieldType == TYPE_NUMERIC ) {
                set_field_type( fieldItem->field, fieldItem->fieldType,
                                fieldItem->fieldArgs.numericArgs.precision,
                                fieldItem->fieldArgs.numericArgs.minValue,
                                fieldItem->fieldArgs.numericArgs.maxValue );
            } else if( fieldItem->fieldType == TYPE_REGEXP ) {
                set_field_type( fieldItem->field, fieldItem->fieldType,
                                fieldItem->fieldArgs.regexp );
            } else if( fieldItem->fieldType == TYPE_IPV4 ) {
                set_field_type( fieldItem->field, fieldItem->fieldType );
            }

            if( fieldItem->maxLen == 0 ) {
                fieldItem->maxLen = width;
            }

            if( fieldItem->maxLen > width ) {
                field_opts_off( fieldItem->field, O_STATIC );
                set_max_field( fieldItem->field, fieldItem->maxLen );
            }

            if( fieldItem->string ) {
                set_field_buffer( fieldItem->field, 0, fieldItem->string );
            }

            set_field_back( fieldItem->field, A_UNDERLINE );
            field_opts_off( fieldItem->field, O_AUTOSKIP );
            detailsFields[i] = fieldItem->field;
            i++;
            break;
        case FIELD_BUTTON:
            break;
        }
    }
    detailsFields[count] = NULL;

    detailsForm = new_form( detailsFields );
    set_form_win( detailsForm, winDetailsForm );
    set_form_sub( detailsForm, winDetailsForm );
    post_form( detailsForm );

    curs_set(1);

    wsyncup( winDetailsForm );

    for( i = 0, item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        if( fieldItem->type == FIELD_LABEL ) {
            mvwprintw( winDetails, fieldItem->starty, fieldItem->startx,
                       fieldItem->string );
        }
    }

    LinkedListUnlock( formList );
    wsyncup( winDetails );
    wrefresh( winFull );
}

bool cursesFormKeyhandle( int ch )
{
    switch( ch ) {
    case 27:    /* Escape */
    case KEY_PPAGE:
    case KEY_NPAGE:
    case 18:    /* Ctrl-R */
        return( TRUE );
    case KEY_DOWN:
        form_driver( detailsForm, REQ_SNEXT_FIELD );
        form_driver( detailsForm, REQ_END_LINE );
        break;
    case KEY_UP:
        form_driver( detailsForm, REQ_SPREV_FIELD );
        form_driver( detailsForm, REQ_END_LINE );
        break;
    case KEY_BACKSPACE:
        form_driver( detailsForm, REQ_DEL_PREV );
        break;
    case KEY_DC:
        form_driver( detailsForm, REQ_DEL_CHAR );
        break;
    case KEY_LEFT:
        form_driver( detailsForm, REQ_PREV_CHAR );
        break;
    case KEY_RIGHT:
        form_driver( detailsForm, REQ_NEXT_CHAR );
        break;
    case KEY_HOME:
        form_driver( detailsForm, REQ_BEG_LINE );
        break;
    case KEY_END:
        form_driver( detailsForm, REQ_END_LINE );
        break;
    default:
        form_driver( detailsForm, ch );
        break;
    }

    return( FALSE );
}

void cursesServerDisplay( void *arg )
{
    IRCServer_t            *server;
    static char             buf[64];
    CursesFieldTypeArgs_t   fieldArgs;

    server = (IRCServer_t *)arg;

    cursesKeyhandleRegister( cursesFormKeyhandle );

    snprintf( buf, 64, "Server Number:  %d", server->serverId );
    cursesFieldAdd( FIELD_LABEL, 0, 0, 0, 0, buf, 0, NULL, NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 1, 0, 0, "Server:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 1, 32, 1, server->server, 64, NULL, NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 2, 0, 0, "Port:", 0, NULL, NULL );
    fieldArgs.integerArgs.precision = 0;
    fieldArgs.integerArgs.minValue  = 1;
    fieldArgs.integerArgs.maxValue  = 65535;
    snprintf( buf, 64, "%d", server->port );
    cursesFieldAdd( FIELD_FIELD, 16, 2, 6, 1, buf, 6, TYPE_INTEGER, 
                    &fieldArgs );
    cursesFieldAdd( FIELD_LABEL, 0, 3, 0, 0, "Password:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 3, 32, 1, server->password, 64, NULL, 
                    NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 4, 0, 0, "Nick:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 4, 32, 1, server->nick, 64, NULL, NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 5, 0, 0, "User Name:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 5, 32, 1, server->username, 64, NULL, 
                    NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 6, 0, 0, "Real Name:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 6, 32, 1, server->realname, 64, NULL, 
                    NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 7, 0, 0, "Nickserv Nick:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 7, 32, 1, server->nickserv, 64, NULL, 
                    NULL );
    cursesFieldAdd( FIELD_LABEL, 0, 8, 0, 0, "Nickserv Msg:", 0, NULL, NULL );
    cursesFieldAdd( FIELD_FIELD, 16, 8, 32, 1, server->nickservmsg, 64, NULL, 
                    NULL );

    cursesFieldAdd( FIELD_LABEL, 0, 9, 0, 0, "Flood Interval:", 0, NULL, NULL );
    snprintf( buf, 64, "%d", server->floodInterval );
    fieldArgs.integerArgs.precision = 0;
    fieldArgs.integerArgs.minValue  = 0;
    fieldArgs.integerArgs.maxValue  = 0x7FFFFFFF;
    cursesFieldAdd( FIELD_FIELD, 16, 9, 20, 1, buf, 20, TYPE_INTEGER, 
                    &fieldArgs );

    cursesFieldAdd( FIELD_LABEL, 0, 10, 0, 0, "Flood Max Time:", 0, NULL, 
                    NULL );
    snprintf( buf, 64, "%d", server->floodMaxTime );
    fieldArgs.integerArgs.precision = 0;
    fieldArgs.integerArgs.minValue  = 0;
    fieldArgs.integerArgs.maxValue  = 0x7FFFFFFF;
    cursesFieldAdd( FIELD_FIELD, 16, 10, 20, 1, buf, 20, TYPE_INTEGER, 
                    &fieldArgs );

    cursesFieldAdd( FIELD_LABEL, 0, 11, 0, 0, "Flood Buffer:", 0, NULL, NULL );
    snprintf( buf, 64, "%d", server->floodBuffer );
    fieldArgs.integerArgs.precision = 0;
    fieldArgs.integerArgs.minValue  = 0;
    fieldArgs.integerArgs.maxValue  = 0x7FFFFFFF;
    cursesFieldAdd( FIELD_FIELD, 16, 11, 20, 1, buf, 20, TYPE_INTEGER, 
                    &fieldArgs );

    cursesFieldAdd( FIELD_LABEL, 0, 12, 0, 0, "Flood Max Line:", 0, NULL, 
                    NULL );
    snprintf( buf, 64, "%d", server->floodMaxLine );
    fieldArgs.integerArgs.precision = 0;
    fieldArgs.integerArgs.minValue  = 0;
    fieldArgs.integerArgs.maxValue  = 0x7FFFFFFF;
    cursesFieldAdd( FIELD_FIELD, 16, 12, 20, 1, buf, 20, TYPE_INTEGER, 
                    &fieldArgs );
}

void cursesChannelDisplay( void *arg )
{
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
