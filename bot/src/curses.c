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
#include <time.h>
#include <ncurses.h>
#include <menu.h>
#include <form.h>
#include <math.h>
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
bool            cursesExited = FALSE;

WINDOW         *winFull;
WINDOW         *winHeader;
WINDOW         *winMenu1;
WINDOW         *winMenu2;
WINDOW         *winDetails;
WINDOW         *winLog;
WINDOW         *winLogScrollbar;
WINDOW         *winLogHScrollbar;
WINDOW         *winTailer;
WINDOW         *winDetailsForm;

FORM           *detailsForm = NULL;
FIELD         **detailsFields = NULL;
int             detailsIndex = -1;
int             detailsPage = 0;
int             detailsFieldCount = 0;
int             detailsTopLine = 0;
int             detailsBottomLine = 0;
int             detailsItemCount = 0;

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
void cursesReloadScreen( void );
void cursesWindowClear( CursesWindow_t window );
void cursesUpdateLines( void );
void cursesFieldChanged( FORM *form );
void cursesNextPage( void *arg, char *string );
void cursesUpdateFormLabels( void );
bool cursesRecurseMenuItemFind( BalancedBTreeItem_t *node, char *string, 
                                int *pIndex );

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
    { &winHeader,        0, 0, 0, 0 },
    { &winMenu1,         0, 0, 0, 0 },
    { &winMenu2,         0, 0, 0, 0 },
    { &winDetails,       0, 0, 0, 0 },
    { &winDetailsForm,   0, 0, 0, 0 },
    { &winLog,           0, 0, 0, 0 },
    { &winLogScrollbar,  0, 0, 0, 0 },
    { &winLogHScrollbar, 0, 0, 0, 0 },
    { &winTailer,        0, 0, 0, 0 }
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
    { 3, -1, "Plugins",  NULL, cursesDoSubMenu, NULL, NULL }
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
static CursesMenuFunc_t cursesCleanupFunc = NULL;

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
    CursesFieldChangeFunc_t fieldChangeFunc;
    void                   *fieldChangeFuncArg;
    CursesSaveFunc_t        saveFunc;
    int                     index;
} CursesField_t;

LinkedList_t   *formList;
LinkedList_t   *formPageList;

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
    wrefresh( winFull );

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
    windows[WINDOW_LOG].width  = x - 1;
    windows[WINDOW_LOG].height = lines + ((y-5) & 1) - 1;

    windows[WINDOW_LOG_SCROLLBAR].startx = x - 1;
    windows[WINDOW_LOG_SCROLLBAR].starty = lines + 3;
    windows[WINDOW_LOG_SCROLLBAR].width  = 1;
    windows[WINDOW_LOG_SCROLLBAR].height = lines + ((y-5) & 1) - 1;

    windows[WINDOW_LOG_HSCROLLBAR].startx = 0;
    windows[WINDOW_LOG_HSCROLLBAR].starty = y - 3;
    windows[WINDOW_LOG_HSCROLLBAR].width  = x - 1;
    windows[WINDOW_LOG_HSCROLLBAR].height = 1;

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

    CursesQ      = QueueCreate(2048);
    CursesLogQ   = QueueCreate(2048);
    formList     = LinkedListCreate();
    formPageList = LinkedListCreate();

    thread_create( &cursesOutThreadId, curses_output_thread, NULL, 
                   "thread_curses_out", NULL );
    thread_create( &cursesInThreadId, curses_input_thread, NULL, 
                   "thread_curses_in", NULL );

#if 0
    for( i = KEY_MIN; i < KEY_MAX; i++ ) {
        if( has_key( i ) ) {
            LogPrint( LOG_DEBUG, "Has key %04o", i );
        }
    }
#endif
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
    int                 logCurr;
    int                 logCount;
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
    int                 key;
    int                 starty;
    int                 maxx, maxy;
    int                 logLineOffset = 0;
    int                 logOffset;

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
            key = item->data.key.keystroke;
            if( !inSubMenuFunc || !currDetailKeyhandler ||
                (key = currDetailKeyhandler( key )) ) {
                switch( key ) {
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
                case KEY_F(1):
                    getmaxyx( winLog, maxy, maxx );
                    logLineOffset -= (maxx - 1) / 2;
                    if( logLineOffset < 0 ) {
                        logLineOffset = 0;
                    }
                    break;
                case KEY_F(2):
                    getmaxyx( winLog, maxy, maxx );
                    logLineOffset += (maxx - 1) / 2;
                    break;
                case 10:    /* Enter */
                case KEY_RIGHT:
                    currItem = current_item( menus[currMenuId+1]->menu );
                    menuItem = (CursesMenuItem_t *)item_userptr(currItem);
                    pos_menu_cursor( menus[currMenuId+1]->menu );
                    if( menuItem->menuFunc ) {
                        if( menuItem->menuFunc != cursesDoSubMenu ) {
                            inSubMenuFunc = TRUE;
                            detailsIndex = -1;
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
                        detailsTopLine = 0;
                        detailsBottomLine = 0;
                        if( cursesCleanupFunc ) {
                            cursesCleanupFunc(NULL);
                        }
                        cursesCleanupFunc = NULL;
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

            getmaxyx( *windows[i].window, maxy, maxx );
            if( i == WINDOW_DETAILS ) {
                detailsBottomLine = 0;
            }

            for( listItem = textEntries[i]->head; listItem; 
                 listItem = listItem->next ) {
                textItem = (CursesText_t *)listItem;

                if( i == WINDOW_DETAILS ) {
                    starty = textItem->y - detailsTopLine;
                    if( (starty < 0 || starty >= maxy) && 
                        textItem->align != ALIGN_WRAP ) {
                        if( starty >= maxy ) {
                            detailsBottomLine = maxy;
                        }
                        continue;
                    }
                } else {
                    starty = textItem->y;
                }

                switch( textItem->align ) {
                case ALIGN_LEFT:
                    len = MIN( textItem->len, maxx - textItem->x );
                    mvwaddnstr( *windows[i].window, starty, textItem->x, 
                                textItem->string, len );
                    if( i == WINDOW_DETAILS ) {
                        detailsBottomLine = MAX( detailsBottomLine, starty );
                    }
                    break;
                case ALIGN_RIGHT:
                    len = MIN( textItem->len, maxx - textItem->x );
                    mvwaddnstr( *windows[i].window, starty, 
                                maxx - textItem->x - textItem->len, 
                                textItem->string, len );
                    if( i == WINDOW_DETAILS ) {
                        detailsBottomLine = MAX( detailsBottomLine, starty );
                    }
                    break;
                case ALIGN_CENTER:
                    len = MIN( textItem->len, maxx - textItem->x );
                    mvwaddnstr( *windows[i].window, starty, 
                                ((maxx - len) / 2) + textItem->x, 
                                textItem->string, len );
                    if( i == WINDOW_DETAILS ) {
                        detailsBottomLine = MAX( detailsBottomLine, starty );
                    }
                    break;
                case ALIGN_FROM_CENTER:
                    len = MIN( textItem->len, maxx - textItem->x );
                    mvwaddnstr( *windows[i].window, starty, 
                                (maxx / 2) + textItem->x, textItem->string, 
                                len );
                    if( i == WINDOW_DETAILS ) {
                        detailsBottomLine = MAX( detailsBottomLine, starty );
                    }
                    break;
                case ALIGN_WRAP:
                    string    = textItem->string;
                    len       = textItem->len;
                    y         = textItem->y - detailsTopLine;
                    linewidth = windows[i].width - textItem->x;


                    linelen = 0;
                    word = string;
                    while( len && *string ) {
                        if( len <= linewidth && y >= 0 && y < maxy ) {
                            mvwaddnstr( *windows[i].window, y, textItem->x,
                                        string, len );
                            if( i == WINDOW_DETAILS ) {
                                detailsBottomLine = MAX( detailsBottomLine,
                                                         y );
                            }
                            len = 0;
                            continue;
                        }
                        
                        ch = strpbrk( word, " \t\n\r" );
                        if( !ch ) {
                            if( linelen ) {
                                /*
                                 * We accumulated words
                                 */
                                if( y >= 0 && y < maxy ) {
                                    mvwaddnstr( *windows[i].window, y, 
                                                textItem->x, string, linelen );
                                    if( i == WINDOW_DETAILS ) {
                                        detailsBottomLine = 
                                            MAX( detailsBottomLine, y );
                                    }
                                }
                                string += linelen + 1;
                                word    = string;
                                len    -= linelen + 1;
                            } else {
                                /* 
                                 * no whitespace left... chop a word in the
                                 * middle and nastily hyphenate it
                                 */
                                if( y >= 0 && y < maxy ) {
                                    mvwaddnstr( *windows[i].window, y, 
                                                textItem->x, string, 
                                                linewidth-2 );
                                    mvwaddnstr( *windows[i].window, y, 
                                                linewidth-2, "-", 1 );
                                    if( i == WINDOW_DETAILS ) {
                                        detailsBottomLine = 
                                            MAX( detailsBottomLine, y );
                                    }
                                }
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
                                if( y >= 0 && y < maxy ) {
                                    mvwaddnstr( *windows[i].window, y, 
                                                textItem->x, string, 
                                                linewidth-2 );
                                    mvwaddnstr( *windows[i].window, y, 
                                                linewidth-2, "-", 1 );
                                    if( i == WINDOW_DETAILS ) {
                                        detailsBottomLine = 
                                            MAX( detailsBottomLine, y );
                                    }
                                }
                                string += linewidth - 2;
                                word    = string;
                                len    -= linewidth - 2;
                            } else {
                                if( y >= 0 && y < maxy ) {
                                    mvwaddnstr( *windows[i].window, y, 
                                                textItem->x, string, linelen );
                                    if( i == WINDOW_DETAILS ) {
                                        detailsBottomLine = 
                                            MAX( detailsBottomLine, y );
                                    }
                                }
                                string += linelen + 1;
                                len    -= linelen + 1;
                                linelen = 0;
                                word    = string;
                            }
                            y++;
                        } else if( *ch == '\n' || *ch == '\r' ) {
                            /* OK, we have whitespace */
                            linelen = ch - string;
                            if( y >= 0 && y < maxy ) {
                                mvwaddnstr( *windows[i].window, y, textItem->x,
                                            string, linelen );
                                if( i == WINDOW_DETAILS ) {
                                    detailsBottomLine = MAX( detailsBottomLine,
                                                             y );
                                }
                            }

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

        /* UpdateLogs */
        QueueLock( CursesLogQ );
        getmaxyx( winLog, lines, x );

        count = QueueUsed( CursesLogQ );
        logCount = count;
        if( count > lines ) {
            if( !scrolledBack ) {
                logTop = (CursesLogQ->head + CursesLogQ->numElements - 
                          lines) & CursesLogQ->numMask;
            }
            count = lines;
        } else {
            logTop = CursesLogQ->tail;
        }

        logCurr = (logTop + CursesLogQ->numElements + count - 
                   CursesLogQ->tail) & CursesLogQ->numMask;

        wclear( winLog );

        for( i = 0; i < count; i++ ) {
            logEntry = (logTop + i) & CursesLogQ->numMask;
            logItem = (CursesLog_t *)CursesLogQ->itemTable[logEntry];
            len = strlen( logItem->message );
            if( len > logLineOffset ) {
                mvwprintw( winLog, i, 0, "%s", 
                           &logItem->message[logLineOffset] );
            }
        }

        getmaxyx( winLogScrollbar, maxy, maxx );
        for( i = 0; i < maxy; i++ ) {
            y = (logCurr * (maxy - 1)) - ((logCount - 1) * i);
            if( y >= 0 && y < logCount - 1 ) {
                mvwaddch( winLogScrollbar, i, 0, ACS_BLOCK );
            } else {
                mvwaddch( winLogScrollbar, i, 0, ACS_CKBOARD );
            }
        }

        getmaxyx( winLogHScrollbar, maxy, maxx );
        logOffset = (2 * logLineOffset) / (maxx - 1);
        if( logOffset > 4 ) {
            logOffset = 4;
        }

        for( i = 0; i < maxx; i++ ) {
            x = (logOffset * (maxx - 1)) - (i * 4);
            if( x >= 0 && x < 4 ) {
                mvwaddch( winLogHScrollbar, 0, i, ACS_BLOCK );
            } else {
                mvwaddch( winLogHScrollbar, 0, i, ACS_CKBOARD );
            }
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
        detailsIndex = field_index( current_field( detailsForm ) );
        unpost_form( detailsForm );
        free_form( detailsForm );

        for( i = 0; detailsFields && detailsFields[i]; i++ ) {
            free_field( detailsFields[i] );
            detailsFields[i] = NULL;
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

    if( cursesExited ) {
        return;
    }

    cursesExited = TRUE;
    curs_set(1);
    echo();
    nl();
    wrefresh( winFull );
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
            LogPrint(LOG_DEBUG, "input error: %s", strerror(errno));
            sleep( 1 );
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

    if( Daemon || !string ) {
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
    pos_menu_cursor( menus[currMenuId+1]->menu );
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

int cursesDetailsKeyhandle( int ch )
{
    int         x, y;

    switch( ch ) {
    case KEY_LEFT:
    case KEY_PPAGE:
    case KEY_NPAGE:
    case 18:    /* Ctrl-R */
    case KEY_F(1):
    case KEY_F(2):
        return( ch );
    case KEY_UP:
        detailsTopLine--;
        if( detailsTopLine < 0 ) {
            detailsTopLine = 0;
        }
        wclear( winDetails );
        break;
    case KEY_DOWN:
        getmaxyx( winDetails, y, x );
        if( detailsBottomLine > y - 2 ) {
            detailsTopLine++;
            wclear( winDetails );
        }
        break;
    case KEY_RIGHT:
    default:
        break;
    }

    return( 0 );
}

void cursesKeyhandleRegister( CursesKeyhandleFunc_t func )
{
    if( !func ) {
        return;
    }

    currDetailKeyhandler = func;
}

void cursesFormFieldAdd( int startx, int starty, int width, int height, 
                         char *string, int maxLen, void *fieldType, 
                         CursesFieldTypeArgs_t *fieldArgs, 
                         CursesFieldChangeFunc_t changeFunc, 
                         void *changeFuncArg, CursesSaveFunc_t saveFunc,
                         int index )
{
    CursesItem_t   *cursesItem;
    CursesField_t  *field;

    if( !inSubMenuFunc ) {
        return;
    }

    field = (CursesField_t *)malloc(sizeof(CursesField_t));
    memset( field, 0x00, sizeof(CursesField_t) );
    field->type      = FIELD_FIELD;
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
    field->fieldChangeFunc      = changeFunc;
    field->fieldChangeFuncArg   = changeFuncArg;
    field->saveFunc             = saveFunc;
    field->index                = index;

    LinkedListAdd( formList, (LinkedListItem_t *)field, UNLOCKED, AT_TAIL );
    detailsItemCount++;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_ADD;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesFormLabelAdd( int startx, int starty, char *string )
{
    CursesItem_t   *cursesItem;
    CursesField_t  *field;

    if( !inSubMenuFunc ) {
        return;
    }

    field = (CursesField_t *)malloc(sizeof(CursesField_t));
    memset( field, 0x00, sizeof(CursesField_t) );
    field->type      = FIELD_LABEL;
    field->startx    = startx;
    field->starty    = starty;
    field->string    = strdup(string);
    field->len       = strlen(string);

    LinkedListAdd( formList, (LinkedListItem_t *)field, UNLOCKED, AT_TAIL );
    detailsItemCount++;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_ADD;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesFormCheckboxAdd( int startx, int starty, bool enabled,
                            CursesFieldChangeFunc_t changeFunc,
                            void *changeFuncArg, CursesSaveFunc_t saveFunc,
                            int index )
{
    CursesItem_t   *cursesItem;
    CursesField_t  *field;
    int             x;

    if( !inSubMenuFunc ) {
        return;
    }

    x = startx;
    cursesFormLabelAdd( x++, starty, "[" );

    field = (CursesField_t *)malloc(sizeof(CursesField_t));
    memset( field, 0x00, sizeof(CursesField_t) );
    field->type      = FIELD_CHECKBOX;
    field->startx    = x++;
    field->starty    = starty;
    field->string    = strdup(enabled ? "X" : " ");
    field->len       = strlen(field->string);
    field->fieldChangeFunc      = changeFunc;
    field->fieldChangeFuncArg   = changeFuncArg;
    field->saveFunc             = saveFunc;
    field->index                = index;

    LinkedListAdd( formList, (LinkedListItem_t *)field, UNLOCKED, AT_TAIL );
    detailsItemCount++;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_ADD;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );

    cursesFormLabelAdd( x++, starty, "]" );
}

void cursesFormButtonAdd( int startx, int starty, char *string,
                          CursesFieldChangeFunc_t changeFunc,
                          void *changeFuncArg )
{
    CursesItem_t   *cursesItem;
    CursesField_t  *field;

    if( !inSubMenuFunc ) {
        return;
    }

    field = (CursesField_t *)malloc(sizeof(CursesField_t));
    memset( field, 0x00, sizeof(CursesField_t) );
    field->type      = FIELD_BUTTON;
    field->startx    = startx;
    field->starty    = starty;
    field->string    = strdup(string);
    field->len       = strlen(string);
    field->fieldChangeFunc      = changeFunc;
    field->fieldChangeFuncArg   = changeFuncArg;

    LinkedListAdd( formList, (LinkedListItem_t *)field, UNLOCKED, AT_TAIL );
    detailsItemCount++;

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
            detailsFields[i] = NULL;
        }
        free( detailsFields );
        detailsFields = NULL;
        detailsForm = NULL;
        detailsIndex = -1;
    }

    LinkedListLock( formList );
    LinkedListLock( formPageList );

    while( (item = formList->head) ) {
        fieldItem = (CursesField_t *)item;

        LinkedListRemove( formList, item, LOCKED );
        free( fieldItem->string );
        free( fieldItem );
    }

    while( (item = formPageList->head) ) {
        fieldItem = (CursesField_t *)item;

        LinkedListRemove( formPageList, item, LOCKED );
        free( fieldItem->string );
        free( fieldItem );
    }

    LinkedListUnlock( formPageList );
    LinkedListUnlock( formList );

    detailsItemCount = 0;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_FORM_ITEM_REMOVE;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesFormRegenerate( void )
{
    LinkedListItem_t       *item;
    CursesField_t          *fieldItem;
    int                     count;
    int                     i = 0;
    int                     x, y;
    int                     width;
    static char            *checkBoxStrings[] = { " ", "X", NULL };
    int                     pageStart;
    int                     pageNum;
    int                     pageCount;
    int                     maxy;
    bool                    newPage;
    int                     starty;

    if( !inSubMenuFunc || detailsItemCount == 0 ) {
        return;
    }

    getmaxyx( winDetails, y, x );

    LinkedListLock( formList );
    LinkedListLock( formPageList );

    while( (item = formPageList->head) ) {
        fieldItem = (CursesField_t *)item;

        LinkedListRemove( formPageList, item, LOCKED );
        free( fieldItem->string );
        free( fieldItem );
    }

    maxy = 0;
    for( count = 0, item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        if( fieldItem->type != FIELD_LABEL ) {
            count++;
        }

        if( fieldItem->height ) {
            maxy = MAX( fieldItem->starty + fieldItem->height - 1, maxy );
        } else {
            maxy = MAX( fieldItem->starty, maxy );
        }
    }

    if( detailsForm ) {
        detailsIndex = field_index( current_field( detailsForm ) );
        detailsPage  = form_page( detailsForm );
        unpost_form( detailsForm );
        free_form( detailsForm );
        for( i = 0; detailsFields && detailsFields[i]; i++ ) {
            free_field( detailsFields[i] );
            detailsFields[i] = NULL;
        }
    }

    pageCount = (maxy + y - 1) / (y - 1);
    newPage = FALSE;
    
    detailsFieldCount = count + pageCount + 1;
    detailsFields = (FIELD **)realloc( detailsFields, detailsFieldCount *
                                        sizeof(FIELD *) );
    memset( detailsFields, 0x00, detailsFieldCount * sizeof(FIELD *) );
    
    i = 0;
    for( pageNum = 0, pageStart = 0; pageNum < pageCount; 
         pageNum++, pageStart += y-1 ) {
        for( item = formList->head; item; item = item->next ) {
            fieldItem = (CursesField_t *)item;

            starty = fieldItem->starty - pageStart;

            if( starty < 0 || starty > pageStart + y - 2 )
            {
                continue;
            }
            switch( fieldItem->type ) {
            case FIELD_LABEL:
                continue;
            case FIELD_FIELD:
                width = fieldItem->width;
                if( width + fieldItem->startx > x - 1 ) {
                    width = x - fieldItem->startx - 1;
                }
                fieldItem->field = new_field( fieldItem->height, width,
                                              fieldItem->starty - pageStart,  
                                              fieldItem->startx,
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

                set_field_userptr( fieldItem->field, fieldItem );
                set_field_status( fieldItem->field, FALSE );

                set_field_back( fieldItem->field, A_UNDERLINE );
                field_opts_off( fieldItem->field, O_AUTOSKIP );
                if( newPage ) {
                    set_new_page( fieldItem->field, TRUE );
                }
                detailsFields[i] = fieldItem->field;
                i++;
                break;
            case FIELD_CHECKBOX:
                fieldItem->field = new_field( 1, 1, 
                                              fieldItem->starty - pageStart, 
                                              fieldItem->startx, 0, 0 );
                set_field_type( fieldItem->field, TYPE_ENUM, checkBoxStrings, 0,
                                0 );

                if( fieldItem->string ) {
                    set_field_buffer( fieldItem->field, 0, fieldItem->string );
                }

                set_field_userptr( fieldItem->field, fieldItem );
                set_field_status( fieldItem->field, FALSE );

                set_field_back( fieldItem->field, A_UNDERLINE );
                field_opts_off( fieldItem->field, O_AUTOSKIP );
                if( newPage ) {
                    set_new_page( fieldItem->field, TRUE );
                }
                detailsFields[i] = fieldItem->field;
                i++;
                break;
            case FIELD_BUTTON:
                fieldItem->field = new_field( 1, fieldItem->len, 
                                              fieldItem->starty - pageStart, 
                                              fieldItem->startx, 0, 0 );

                if( fieldItem->string ) {
                    set_field_buffer( fieldItem->field, 0, fieldItem->string );
                }

                set_field_userptr( fieldItem->field, fieldItem );

                set_field_back( fieldItem->field, A_REVERSE );
                field_opts_off( fieldItem->field, O_AUTOSKIP );
                if( newPage ) {
                    set_new_page( fieldItem->field, TRUE );
                }
                detailsFields[i] = fieldItem->field;
                i++;
                break;
            }
            newPage = FALSE;
        }

        if( pageCount > 1 ) {
            fieldItem = (CursesField_t *)malloc(sizeof(CursesField_t));
            memset( fieldItem, 0x00, sizeof(CursesField_t) );
            fieldItem->type      = FIELD_BUTTON;
            fieldItem->starty    = y - 1;
            fieldItem->string    = strdup("Next Page");
            fieldItem->len       = strlen(fieldItem->string);
            fieldItem->startx    = (x - fieldItem->len) / 2;
            fieldItem->fieldChangeFunc      = cursesNextPage;
            fieldItem->fieldChangeFuncArg   = NULL;

            LinkedListAdd( formPageList, (LinkedListItem_t *)fieldItem, LOCKED, 
                           AT_TAIL );

            fieldItem->field = new_field( 1, fieldItem->len, 
                                          fieldItem->starty, 
                                          fieldItem->startx, 0, 0 );

            if( fieldItem->string ) {
                set_field_buffer( fieldItem->field, 0, fieldItem->string );
            }

            set_field_userptr( fieldItem->field, fieldItem );

            set_field_back( fieldItem->field, A_REVERSE );
            field_opts_off( fieldItem->field, O_AUTOSKIP );
            detailsFields[i] = fieldItem->field;
            i++;
            newPage = TRUE;
        }
    }
    detailsFields[i] = NULL;

    detailsForm = new_form( detailsFields );
    set_form_win( detailsForm, winDetailsForm );
    set_form_sub( detailsForm, winDetailsForm );
    set_field_term( detailsForm, cursesFieldChanged );
    post_form( detailsForm );

    curs_set(1);

    wsyncup( winDetailsForm );

    cursesUpdateFormLabels();

    LinkedListUnlock( formPageList );
    LinkedListUnlock( formList );

    if( detailsPage == -1 ) {
        detailsPage = 0;
    }

    if( detailsPage >= pageCount ) {
        detailsPage = pageCount - 1;
    }
    set_form_page( detailsForm, detailsPage );

    if( detailsIndex == -1 ) {
        detailsIndex = 0;
    }

    if( detailsIndex >= count + pageCount ) {
        detailsIndex = count + pageCount - 1;
    }

    set_current_field( detailsForm, detailsFields[detailsIndex] );

    wsyncup( winDetails );
    wrefresh( winFull );
}

int cursesFormKeyhandle( int ch )
{
    FIELD          *field;
    CursesField_t  *fieldItem;

    field = current_field( detailsForm );
    fieldItem = (CursesField_t *)field_userptr( field );

    switch( ch ) {
    case 27:    /* Escape */
    case KEY_PPAGE:
    case KEY_NPAGE:
    case 18:    /* Ctrl-R */
    case KEY_F(1):
    case KEY_F(2):
        return( ch );
    case KEY_DOWN:
    case 9:     /* Tab */
        form_driver( detailsForm, REQ_SNEXT_FIELD );
        form_driver( detailsForm, REQ_END_LINE );
        break;
    case KEY_UP:
        form_driver( detailsForm, REQ_SPREV_FIELD );
        form_driver( detailsForm, REQ_END_LINE );
        break;
    case KEY_BACKSPACE:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_DEL_PREV );
        }
        break;
    case KEY_DC:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_DEL_CHAR );
        }
        break;
    case KEY_LEFT:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_PREV_CHAR );
        }
        break;
    case KEY_RIGHT:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_NEXT_CHAR );
        }
        break;
    case KEY_HOME:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_BEG_LINE );
        }
        break;
    case KEY_END:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, REQ_END_LINE );
        }
        break;
    case 32:    /* Space */
    case 10:    /* Enter */
        if ( fieldItem->type == FIELD_CHECKBOX ) {
            form_driver( detailsForm, REQ_NEXT_CHOICE );
        } else if( fieldItem->type == FIELD_BUTTON ) {
            if( fieldItem->fieldChangeFunc ) {
                fieldItem->fieldChangeFunc( fieldItem->fieldChangeFuncArg,
                                            NULL );
                if( !detailsForm ) {
                    return( KEY_LEFT );
                } else if ( detailsForm == (void *)1 ) {
                    detailsForm = NULL;
                    cursesFormRegenerate();
                }
            }
        } else {
            form_driver( detailsForm, ch );
        }
    default:
        if( fieldItem->type == FIELD_FIELD ) {
            form_driver( detailsForm, ch );
        }
        break;
    }

    return( 0 );
}

void cursesFieldChanged( FORM *form )
{
    FIELD          *field;
    CursesField_t  *fieldItem;

    field = current_field( form );
    fieldItem = (CursesField_t *)field_userptr(field);

    if( fieldItem->type == FIELD_BUTTON ) {
        return;
    }

    free( fieldItem->string );
    fieldItem->string = strdup( field_buffer( field, 0 ) );

    if( fieldItem->fieldChangeFunc ) {
        fieldItem->fieldChangeFunc( fieldItem->fieldChangeFuncArg, 
                                    fieldItem->string );
    }
}

void cursesCancel( void *arg, char *string )
{
    cursesFormClear();
}

void cursesSave( void *arg, char *string )
{
    LinkedListItem_t       *item;
    CursesField_t          *fieldItem;
    CursesSaveFunc_t        saveFunc = NULL;

    LinkedListLock( formList );

    for( item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        if( (fieldItem->type == FIELD_FIELD || 
             fieldItem->type == FIELD_CHECKBOX) && fieldItem->saveFunc ) {
            if( field_status( fieldItem->field ) ) {
                set_field_status( fieldItem->field, FALSE );
                string = field_buffer( fieldItem->field, 0 );
                fieldItem->saveFunc( arg, fieldItem->index, string );
                saveFunc = fieldItem->saveFunc;
            }
        }
    }

    LinkedListUnlock( formList );

    if( saveFunc ) {
        saveFunc( arg, -1, "" );
    }
}

void cursesNextPage( void *arg, char *string )
{
    form_driver( detailsForm, REQ_NEXT_PAGE );
    detailsPage = form_page( detailsForm );

    LinkedListLock( formList );
    cursesUpdateFormLabels();
    LinkedListUnlock( formList );
}

void cursesUpdateFormLabels( void )
{
    int                     i;
    LinkedListItem_t       *item;
    CursesField_t          *fieldItem;
    int                     x, y;
    int                     starty;
    int                     width;

    getmaxyx( winDetails, y, x );

    /* Assumes formList is locked */
    for( i = 0, item = formList->head; item; item = item->next ) {
        fieldItem = (CursesField_t *)item;
        if( fieldItem->type == FIELD_LABEL ) {
            starty = fieldItem->starty - (detailsPage * (y-1));
            if( starty >= 0 && starty < y - 1 ) {
                width = MIN( x - fieldItem->startx, strlen(fieldItem->string));
                mvwaddnstr( winDetails, starty, fieldItem->startx,
                            fieldItem->string, width );
            }
        }
    }
}

void cursesFormDisplay( void *arg, CursesFormItem_t *items, int count,
                        CursesSaveFunc_t saveFunc )
{
    static char             buf[1024];
    CursesFormItem_t       *item;
    int                     i;
    FIELDTYPE              *fieldtype;
    int                     len;
    void                   *buttonArg;
    struct tm               tm;

    cursesKeyhandleRegister( cursesFormKeyhandle );

    for( i = 0; i < count; i++ ) {
        item = &items[i];

        switch( item->type ) {
        case FIELD_LABEL:
            if( item->offset != -1 && item->offsetType != FA_NONE ) {
                switch( item->offsetType ) {
                case FA_STRING:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, char *) );
                    break;
                case FA_INTEGER:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, int) );
                    break;
                case FA_LONG_INTEGER:
                case FA_LONG_INTEGER_HEX:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, long int) );
                    break;
                case FA_TIME_T:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, time_t) );
                    break;
                case FA_BOOL:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, bool) );
                    break;
                case FA_CHAR:
                    snprintf( buf, 1024, item->format,
                              ATOFFSET(arg, item->offset, char) );
                    break;
                case FA_SERVER:
                    snprintf( buf, 1024, item->format,
                        ATOFFSET(arg, item->offset, IRCServer_t *)->serverId );
                    break;
                case FA_TIMESTAMP:
                    localtime_r((const time_t *)
                                  &ATOFFSET(arg,item->offset,time_t), &tm);
                    strftime( buf, 1024, "%a, %e, %b %Y %H:%M:%S %Z", &tm );
                    break;
                default:
                    buf[0] = '\0';
                }
            } else {
                snprintf( buf, 1024, item->format );
            }
            cursesFormLabelAdd( item->startx, item->starty, buf );
            break;
        case FIELD_FIELD:
            if( item->maxLen == 0 ) {
                len = 1024;
            } else {
                len = MIN(1024, item->maxLen + 1);
            }

            switch( item->offsetType ) {
            case FA_STRING:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, char *) );
                break;
            case FA_INTEGER:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, int) );
                break;
            case FA_LONG_INTEGER:
            case FA_LONG_INTEGER_HEX:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, long int) );
                break;
            case FA_TIME_T:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, time_t) );
                break;
            case FA_BOOL:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, bool) );
                break;
            case FA_CHAR:
                snprintf( buf, len, item->format,
                          ATOFFSET(arg, item->offset, char) );
                break;
            case FA_SERVER:
                snprintf( buf, len, item->format,
                        ATOFFSET(arg, item->offset, IRCServer_t *)->serverId );
                break;
            case FA_TIMESTAMP:
                localtime_r((const time_t *)&ATOFFSET(arg,item->offset,time_t),
                            &tm);
                strftime( buf, len, "%a, %e, %b %Y %H:%M:%S %Z", &tm );
                break;
            default:
                buf[0] = '\0';
            }

            switch( item->fieldType ) {
            case FT_NONE:
            default:
                fieldtype = NULL;
                break;
            case FT_ALNUM:
                fieldtype = TYPE_ALNUM;
                break;
            case FT_ALPHA:
                fieldtype = TYPE_ALPHA;
                break;
            case FT_ENUM:
                fieldtype = TYPE_ENUM;
                break;
            case FT_INTEGER:
                fieldtype = TYPE_INTEGER;
                break;
            case FT_NUMERIC:
                fieldtype = TYPE_NUMERIC;
                break;
            case FT_REGEXP:
                fieldtype = TYPE_REGEXP;
                break;
            case FT_IPV4:
                fieldtype = TYPE_IPV4;
                break;
            }

            cursesFormFieldAdd( item->startx, item->starty, item->width,
                                item->height, buf, item->maxLen, fieldtype,
                                &item->fieldArgs, item->changeFunc,
                                item->changeFuncArg, saveFunc, i );
            break;
        case FIELD_CHECKBOX:
            cursesFormCheckboxAdd( item->startx, item->starty,
                                   ATOFFSET(arg, item->offset, bool),
                                   item->changeFunc, item->changeFuncArg,
                                   saveFunc, i );
            break;
        case FIELD_BUTTON:
            buttonArg = (item->changeFuncArg == (void *)(-1) ?  arg : 
                         item->changeFuncArg);
            cursesFormButtonAdd( item->startx, item->starty, item->format,
                                 item->changeFunc, buttonArg );
            break;
        }
    }
}

void cursesFormRevert( void *arg, CursesFormItem_t *items, int count,
                       CursesSaveFunc_t saveFunc )
{
    FIELD          *field;
    CursesField_t  *fieldItem;

    field = current_field( detailsForm );
    fieldItem = (CursesField_t *)field_userptr(field);

    fieldItem->fieldChangeFunc = NULL;
    cursesFormClear();
    cursesFormDisplay( arg, items, count, saveFunc );
    detailsForm = (void *)1;
}

void cursesSaveOffset( void *arg, int index, CursesFormItem_t *items,
                       int itemCount, char *string )
{
    CursesFormItem_t       *item;
    char                   *buffer;

    if( index < 0 || index >= itemCount ) {
        return;
    }
    item = &items[index];

    if( item->offset == -1 ) {
        return;
    }

#if 0
    LogPrint( LOG_DEBUG, "arg: %p, index %d, offset %d, string: \"%s\"", 
              arg, index, item->offset, string );
#endif

    switch( item->offsetType ) {
    case FA_STRING:
        free( ATOFFSET(arg, item->offset, char *) );
        if( !strcasecmp(string, "(NULL)") ) {
            ATOFFSET(arg, item->offset, char *) = NULL;
        } else {
            ATOFFSET(arg, item->offset, char *) = strdup( string );
            buffer = ATOFFSET(arg, item->offset, char *);
            while( buffer[strlen(buffer)-1] == ' ' ) {
                buffer[strlen(buffer)-1] = '\0';
            }
        }
        break;
    case FA_INTEGER:
        ATOFFSET(arg, item->offset, int) = atoi( string );
        break;
    case FA_LONG_INTEGER:
        ATOFFSET(arg, item->offset, long int) = atol( string );
        break;
    case FA_LONG_INTEGER_HEX:
        ATOFFSET(arg, item->offset, long int) = strtol( string, NULL, 16 );
        break;
    case FA_TIME_T:
        ATOFFSET(arg, item->offset, time_t) = atol( string );
        break;
    case FA_BOOL:
        ATOFFSET(arg, item->offset, bool) = ( *string == 'X' ? TRUE : FALSE );
        break;
    case FA_CHAR:
        ATOFFSET(arg, item->offset, char) = *string;
        break;
    case FA_SERVER:
        ATOFFSET(arg, item->offset, IRCServer_t *) = 
                                                 FindServerNum( atoi(string) );
        break;
    default:
        return;
    }
}

void cursesRegisterCleanupFunc( CursesMenuFunc_t callback )
{
    cursesCleanupFunc = callback;
}

int cursesMenuItemFind( int level, int menuId, char *string )
{
    CursesMenuItem_t       *menuItem;
    BalancedBTree_t        *tree;
    int                     index;

    if( Daemon ) {
        return( -1 );
    }

    if( level == 1 ) {
        tree = menu1NumTree;
    } else {
        menuItem = cursesMenu1Find( menuId );
        if( !menuItem ) {
            return( -1 );
        }

        tree = menuItem->subMenuTree;
    }

    BalancedBTreeLock( tree );
    index = -1;
    cursesRecurseMenuItemFind( tree->root, string, &index );
    BalancedBTreeUnlock( tree );

    if( level == 1 && index != -1 ) {
        index += menu1StaticCount;
    }

#if 0
    LogPrint( LOG_DEBUG, "Found \"%s\" in menu %d at %d", string, menuId, 
              index );
#endif
    return( index );
}

bool cursesRecurseMenuItemFind( BalancedBTreeItem_t *node, char *string, 
                                int *pIndex )
{
    CursesMenuItem_t       *menuItem;

    if( !node || !pIndex ) {
        return( FALSE );
    }

    if( cursesRecurseMenuItemFind( node->left, string, pIndex ) ) {
        return( TRUE );
    }

    (*pIndex)++;
    menuItem = (CursesMenuItem_t *)node->item;
    if( !strcmp( menuItem->string, string ) ) {
        return( TRUE );
    }

    return( cursesRecurseMenuItemFind( node->right, string, pIndex ) );
}

void cursesMenuSetIndex( int menuId, int index )
{
    menus[menuId+1]->current = index;
    set_current_item( menus[menuId+1]->menu, menus[menuId+1]->items[index] );
#if 0
    LogPrint( LOG_DEBUG, "Set current item in menu %d to %d", menuId, index );
#endif
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
