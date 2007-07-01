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

void *curses_output_thread( void *arg );
void *curses_input_thread( void *arg );
void cursesWindowSet( void );
void cursesMenuInitialize( void );
void cursesMenuRegenerate( void );
int cursesItemCount( BalancedBTree_t *tree );
int cursesItemCountRecurse( BalancedBTreeItem_t *node );
void cursesSubmenuRegenerate( int menuId, BalancedBTree_t *tree );
void cursesItemAdd( ITEM **items, BalancedBTree_t *tree, int start );
int cursesItemAddRecurse( ITEM **items, BalancedBTreeItem_t *node, int start );
void cursesSubmenuAddAll( BalancedBTreeItem_t *node );
void cursesAtExit( void );
void cursesReloadScreen( void );

typedef enum {
    CURSES_TEXT_ADD,
    CURSES_TEXT_REMOVE,
    CURSES_WINDOW_CLEAR,
    CURSES_LOG_MESSAGE,
    CURSES_MENU_ITEM_ADD,
    CURSES_MENU_ITEM_REMOVE,
    CURSES_KEYSTROKE,
    CURSES_SIGNAL
} CursesType_t;


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

LinkedList_t       *textEntries[WINDOW_COUNT];


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

typedef struct {
    MENU           *menu;
    ITEM          **items;
    int             itemCount;
    bool            posted;
    int             current;
} CursesMenu_t;

static CursesMenu_t   **menus = NULL;
static int              menusCount = 0;

static ProtectedData_t  *nextMenuId;
static int               currMenuId = -1;

CursesMenuItem_t *cursesMenu1Find( int menuId );


void cursesWindowSet( void )
{
    int         lines;


    initscr();
    curs_set(0);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    lines = (LINES-5)/2;

    winFull    = newwin( 0, 0, 0, 0 );
    winHeader  = subwin( winFull, 2, COLS, 0, 0 );
    winMenu1   = subwin( winFull, lines, 15, 2, 0 );
    winMenu2   = subwin( winFull, lines, (COLS / 2) - 15, 2, 15 );
    winDetails = subwin( winFull, lines, COLS / 2, 2, COLS / 2 );
    winLog     = subwin( winFull, lines + ((LINES-5) & 1), COLS, lines + 3, 0 );
    winTailer  = subwin( winFull, 2, COLS, LINES-2, 0 );

    wclear( winFull );
    mvwhline( winFull, lines + 2, 0, ACS_HLINE, COLS );
    touchline( winFull, lines + 2, 1 );
    wrefresh( winFull );
}

void curses_start( void )
{
    int         i;

    for( i = 0; i < WINDOW_COUNT; i++ ) {
        textEntries[i] = LinkedListCreate();
    }

    cursesWindowSet();
    cursesMenuInitialize();

    CursesQ    = QueueCreate(2048);
    CursesLogQ = QueueCreate(2048);

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
        case CURSES_MENU_ITEM_ADD:
        case CURSES_MENU_ITEM_REMOVE:
            /* The real work is done in cursesMenuItemAdd */
            cursesMenuRegenerate();
            break;
        case CURSES_KEYSTROKE:
#if 0
            LogPrint( LOG_CRIT, "Keystroke: %d", item->data.key.keystroke );
#endif
            switch( item->data.key.keystroke ) {
            case KEY_UP:
                menu_driver( menus[currMenuId+1]->menu, REQ_UP_ITEM );
                break;
            case KEY_DOWN:
                menu_driver( menus[currMenuId+1]->menu, REQ_DOWN_ITEM );
                break;
            case KEY_PPAGE:
                QueueLock( CursesLogQ );
                lines = ((LINES-5)/2) + ((LINES-5) & 1);
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
                        logTop = (logTop + CursesLogQ->numElements - lines) &
                                 CursesLogQ->numMask;
                    }
                }
                QueueUnlock( CursesLogQ );
                break;
            case KEY_NPAGE:
                if( !scrolledBack ) {
                    break;
                }

                QueueLock( CursesLogQ );
                lines = (LINES-5)/2 + ((LINES-5) & 1);
                count = QueueUsed( CursesLogQ );

                if( count <= lines ) {
                    scrolledBack = FALSE;
                    QueueUnlock( CursesLogQ );
                    break;
                }

                count = (CursesLogQ->head + CursesLogQ->numElements - logTop) &
                        CursesLogQ->numMask;
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
                    menuItem->menuFunc( menuItem->menuFuncArg );
                }
                break;
            case 18:    /* Ctrl-R */
                cursesReloadScreen();
                break;
            case 27:    /* Escape */
            case KEY_LEFT:
                if( currMenuId != -1 ) {
                    pos_menu_cursor( menus[currMenuId+1]->menu );
                    cursesDoSubMenu( &mainMenu );
                }
                break;
            default:
                break;
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
        for( i = 0; i < WINDOW_COUNT; i++ ) {
            LinkedListLock( textEntries[i] );

            for( listItem = textEntries[i]->head; listItem; 
                 listItem = listItem->next ) {
                textItem = (CursesText_t *)listItem;

                mvwaddnstr( *windows[i], textItem->y, textItem->x, 
                            textItem->string, textItem->len );
            }
            LinkedListUnlock( textEntries[i] );

            /* sync updates to the full window */
            wsyncup( *windows[i] );
        }

#if 0
    UpdateLogs:
#endif
        QueueLock( CursesLogQ );
        lines = (LINES-5)/2 + ((LINES-5) & 1);

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
    }

    LogPrintNoArg( LOG_NOTICE, "Ending curses output thread" );
    cursesAtExit();
#if 0
    curs_set(1);
    endwin();
#endif
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
}

void cursesAtExit( void )
{
    curs_set(1);
    echo();
    nl();
    wmove( winFull, LINES, 0);
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
                set_menu_format( menus[i]->menu, (LINES-5)/2, 1 );
                set_menu_win( menus[i]->menu, winMenu1 );
                set_menu_sub( menus[i]->menu, winMenu1 );
                menus[i]->posted = TRUE;
            } else {
#if 0
                set_menu_format( menus[i]->menu, 1, (COLS/2) - 15 );
#else
                set_menu_format( menus[i]->menu, (LINES-5)/2, 1 );
#endif
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

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_LOG_MESSAGE;
    cursesItem->data.log.message = strdup(message);
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesTextAdd( CursesWindow_t window, int x, int y, char *string )
{
    CursesItem_t       *cursesItem;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_TEXT_ADD;
    cursesItem->data.text.window = window;
    cursesItem->data.text.x      = x;
    cursesItem->data.text.y      = y;
    cursesItem->data.text.string = strdup( string );
    cursesItem->data.text.len    = strlen( string );
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesTextRemove( CursesWindow_t window, int x, int y )
{
    CursesItem_t       *cursesItem;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_TEXT_REMOVE;
    cursesItem->data.text.window = window;
    cursesItem->data.text.x      = x;
    cursesItem->data.text.y      = y;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

void cursesSigwinch( int signum, void *arg )
{
    CursesItem_t       *cursesItem;

    cursesItem = (CursesItem_t *)malloc(sizeof(CursesItem_t));
    cursesItem->type = CURSES_SIGNAL;
    QueueEnqueueItem( CursesQ, (void *)cursesItem );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
