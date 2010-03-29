/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2010 Gavin Hurlbut
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
* Copyright 2010 Gavin Hurlbut
* All rights reserved
*
*/

#include "environment.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#ifndef __CYGWIN__
#include <execinfo.h>
#include <ucontext.h>
#endif
#include <curl/curl.h>
#include "botnet.h"
#include "protos.h"
#include "release.h"
#include "queue.h"
#include "logging.h"
#include "balanced_btree.h"
#include "clucene.h"


static char ident[] _UNUSED_= 
    "$Id$";

char               *mysql_host;
uint16              mysql_portnum;
char               *mysql_user;
char               *mysql_password;
char               *mysql_db;
bool                Daemon = FALSE;
bool                Debug = FALSE;
bool                GlobalAbort = FALSE;
bool                BotDone = FALSE;
pthread_t           mainThreadId;
BalancedBTree_t    *versionTree;
char               *pthreadsVersion = NULL;


void LogBanner( void );
void MainParseArgs( int argc, char **argv );
void MainDisplayUsage( char *program, char *errorMsg );
void signal_interrupt( int signum, siginfo_t *info, void *secret );
void signal_everyone( int signum, siginfo_t *info, void *secret );
void signal_death( int signum, siginfo_t *info, void *secret );
void MainDelayExit( void );
void do_symbol( void *ptr );

typedef void (*sigAction_t)(int, siginfo_t *, void *);

int main ( int argc, char **argv )
{
    extern QueueObject_t   *IndexQ;
    struct sigaction        sa;
    sigset_t                sigmsk;
    uint32                  count;

    GlobalAbort = false;

    /* Parse the command line options */
    MainParseArgs( argc, argv );

    mainThreadId = pthread_self();

    /* 
     * Setup the sigmasks for this thread (which is the parent to all others).
     * This will propogate to all children.
     */
    sigfillset( &sigmsk );
    sigdelset( &sigmsk, SIGUSR1 );
    sigdelset( &sigmsk, SIGUSR2 );
    sigdelset( &sigmsk, SIGINT );
    sigdelset( &sigmsk, SIGSEGV );
    sigdelset( &sigmsk, SIGILL );
    sigdelset( &sigmsk, SIGFPE );
    pthread_sigmask( SIG_SETMASK, &sigmsk, NULL );

    /* Start up the Logging thread */
    logging_initialize(FALSE);

    thread_register( &mainThreadId, "thread_main", NULL );

    /* Setup signal handler for SIGUSR1 (toggles Debug) */
    sa.sa_sigaction = (sigAction_t)logging_toggle_debug;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;
    sigaction( SIGUSR1, &sa, NULL );

    /* Setup the exit handler */
    atexit( MainDelayExit );

    /* Setup signal handler for SIGINT (shut down cleanly) */
    sa.sa_sigaction = signal_interrupt;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;
    sigaction( SIGINT, &sa, NULL );
    
    /* Setup signal handlers that are to be propogated to all threads */
    sa.sa_sigaction = signal_everyone;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction( SIGUSR2, &sa, NULL );

    /* Setup signal handlers for SEGV, ILL, FPE */
    sa.sa_sigaction = signal_death;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction( SIGSEGV, &sa, NULL );
    sigaction( SIGILL, &sa, NULL );
    sigaction( SIGFPE, &sa, NULL );

    /* Print the startup log messages */
    LogBanner();

    /* Setup the CLucene indexer */
    clucene_init(1);

    /* Setup the MySQL connection */
    db_setup();
    db_check_schema_main();

    db_rebuild_clucene();

    /* Wait for the clucene thread to finish emptying its queue */
    while( (count = QueueUsed( IndexQ )) > 0 ) {
        LogPrint( LOG_INFO, "%d left", count );
        sleep( 1 );
    }

    GlobalAbort = TRUE;

    return(0);
}

void LogBanner( void )
{
    LogPrintNoArg( LOG_CRIT, "importdb from beirdobot  "
                             "(c) 2010 Gavin Hurlbut" );
    LogPrint( LOG_CRIT, "%s", git_version() );
}


void MainParseArgs( int argc, char **argv )
{
    extern char *optarg;
    extern int optind, opterr, optopt;
    int opt;
    int optIndex = 0;
    static struct option longOpts[] = {
        {"help", 0, 0, 'h'},
        {"version", 0, 0, 'V'},
        {"host", 1, 0, 'H'},
        {"user", 1, 0, 'u'},
        {"password", 1, 0, 'p'},
        {"port", 1, 0, 'P'},
        {"database", 1, 0, 'd'},
        {0, 0, 0, 0}
    };

    mysql_host = NULL;
    mysql_portnum = 0;
    mysql_user = NULL;
    mysql_password = NULL;
    mysql_db = NULL;

    while( (opt = getopt_long( argc, argv, "hVH:P:u:p:d:", longOpts, 
                               &optIndex )) != -1 )
    {
        switch( opt )
        {
            case 'h':
                MainDisplayUsage( argv[0], NULL );
                exit( 0 );
                break;
            case 'H':
                if( mysql_host != NULL )
                {
                    free( mysql_host );
                }
                mysql_host = strdup(optarg);
                break;
            case 'P':
                mysql_portnum = atoi(optarg);
                break;
            case 'u':
                if( mysql_user != NULL )
                {
                    free( mysql_user );
                }
                mysql_user = strdup(optarg);
                break;
            case 'p':
                if( mysql_password != NULL )
                {
                    free( mysql_password );
                }
                mysql_password = strdup(optarg);
                break;
            case 'd':
                if( mysql_db != NULL )
                {
                    free( mysql_db );
                }
                mysql_db = strdup(optarg);
                break;
            case 'V':
                LogBanner();
                exit( 0 );
                break;
            case '?':
            case ':':
            default:
                MainDisplayUsage( argv[0], "Unknown option" );
                exit( 1 );
                break;
        }
    }

    if( mysql_host == NULL )
    {
        mysql_host = strdup("localhost");
    }

    if( mysql_portnum == 0 )
    {
        mysql_portnum = 3306;
    }

    if( mysql_user == NULL )
    {
        mysql_user = strdup("beirdobot");
    }

    if( mysql_password == NULL )
    {
        mysql_password = strdup("beirdobot");
    }

    if( mysql_db == NULL )
    {
        mysql_db = strdup("beirdobot");
    }
}

void MainDisplayUsage( char *program, char *errorMsg )
{
    char *nullString = "<program name>";

    LogBanner();

    if( errorMsg != NULL )
    {
        fprintf( stderr, "\n%s\n\n", errorMsg );
    }

    if( program == NULL )
    {
        program = nullString;
    }

    fprintf( stderr, "\nUsage:\n\t%s [-H host] [-P port] [-u user] "
                     "[-p password] [-d database] [-D] [-v]\n\n", program );
    fprintf( stderr, 
               "Options:\n"
               "\t-H or --host\tMySQL host to connect to (default localhost)\n"
               "\t-P or --port\tMySQL port to connect to (default 3306)\n"
               "\t-u or --user\tMySQL user to connect as (default beirdobot)\n"
               "\t-p or --password\tMySQL password to use (default beirdobot)\n"
               "\t-d or --database\tMySQL database to use (default beirdobot)\n"
               "\t-V or --version\tshow the version number and quit\n"
               "\t-h or --help\tshow this help text\n\n" );
}

void signal_interrupt( int signum, siginfo_t *info, void *secret )
{
    extern const char *const    sys_siglist[];
    struct sigaction            sa;

    if( pthread_equal( pthread_self(), mainThreadId ) ) {
        sa.sa_handler = SIG_DFL;
        sigemptyset( &sa.sa_mask );
        sa.sa_flags = SA_RESTART;
        sigaction( SIGINT, &sa, NULL );

        LogPrint( LOG_CRIT, "Received signal: %s", sys_siglist[signum] );
        exit( 0 );
    }
}

#ifdef REG_EIP
 #define OLD_IP REG_EIP
#else
 #ifdef REG_RIP
  #define OLD_IP REG_RIP
 #endif
#endif

void signal_everyone( int signum, siginfo_t *info, void *secret )
{
    extern const char *const    sys_siglist[];
    SigFunc_t                   sigFunc;
    pthread_t                   myThreadId;
    ucontext_t                 *uc;
    void                       *arg;

    uc = (ucontext_t *)secret;
    myThreadId = pthread_self();

#if 0
    if( pthread_equal( myThreadId, mainThreadId ) ) {
        LogPrint( LOG_CRIT, "Received signal: %s", sys_siglist[signum] );
    }
#endif

    sigFunc = ThreadGetHandler( myThreadId, signum, &arg );
    if( sigFunc ) {
        if( signum == SIGUSR2 ) {
#ifdef OLD_IP
            arg = (void *)uc->uc_mcontext.gregs[OLD_IP];
#else
            arg = NULL;
#endif
        }
        sigFunc( signum, arg );
    }

    if( pthread_equal( myThreadId, mainThreadId ) ) {
        ThreadAllKill( signum );
    }
}

void signal_death( int signum, siginfo_t *info, void *secret )
{
    extern const char *const    sys_siglist[];
    ucontext_t                 *uc;
    struct sigaction            sa;

    uc = (ucontext_t *)secret;

    /* Make it so another bad signal will just KILL it */
    sa.sa_handler = SIG_DFL;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;
    sigaction( SIGSEGV, &sa, NULL );
    sigaction( SIGILL, &sa, NULL );
    sigaction( SIGFPE, &sa, NULL );

    LogPrint( LOG_CRIT, "Received signal: %s", sys_siglist[signum] );
#ifdef OLD_IP
    LogPrint( LOG_CRIT, "Faulty Address: %p, from %p", info->si_addr,
                        uc->uc_mcontext.gregs[OLD_IP] );
#else
    LogPrint( LOG_CRIT, "Faulty Address %p, no discernable context",
                        info->si_addr );
#endif

#ifdef OLD_IP
    do_backtrace( signum, (void *)uc->uc_mcontext.gregs[OLD_IP] );
#else
    do_backtrace( signum, NULL );
#endif

    /* Spew all remaining messages */
    LogFlushOutput();

    /* Kill this thing HARD! */
    abort();
}

void do_symbol( void *ptr )
{
#ifndef __CYGWIN__
    void               *array[1];
    char              **strings;

    array[0] = ptr;
    strings = backtrace_symbols( array, 1 );

    LogPrint( LOG_DEBUG, "%s", strings[0] );

    free( strings );
#endif
}

void do_backtrace( int signum, void *ip )
{
#ifndef __CYGWIN__
    void               *array[100];
    size_t              size;
    char              **strings;
    size_t              i;
    char               *name;
    static char        *unknown = "unknown";

    if( ip ) {
        /* This was a signal, so print the thread name */
        name = thread_name( pthread_self() );
        if( !name ) {
            name = unknown;
        }
        LogPrint( LOG_DEBUG, "Thread: %s backtrace", name );
    } else {
        name = NULL;
    }

    size = backtrace( array, 100 );

#if 0
    /* replace the sigaction/pthread_kill with the caller's address */
    if( ip ) {
        array[1] = ip;
    }
#endif

    strings = backtrace_symbols( array, size );

    LogPrint( LOG_DEBUG, "%s%sObtained %zd stack frames.", 
                         (name ? name : ""), (name ? ": " : ""), size );

    for( i = 0; i < size; i++ ) {
        LogPrint( LOG_DEBUG, "%s%s%s", (name ? name : ""), (name? ": " : ""),
                             strings[i] );
    }

    free( strings );
#endif
}

void MainDelayExit( void )
{
    int         i;

    LogPrintNoArg( LOG_CRIT, "Shutting down" );

    /* Signal to all that we are aborting */
    BotDone = FALSE;

    GlobalAbort = true;

    /* Send out signals from all queues waking up anything waiting on them so
     * the listeners can unblock and die
     */
    QueueKillAll();

    /* Delay to allow all the other tasks to finish (esp. logging!) */
    for( i = 2; i && !BotDone; i-- ) {
        sleep(1);
    }

    LogPrintNoArg(LOG_DEBUG, "Shutdown complete!" );
    LogFlushOutput();

    /* And finally... die */
    _exit( 0 );
}

/* Stubbed */
pthread_t           cursesOutThreadId;
BalancedBTree_t    *ServerTree = NULL;
IRCChannel_t       *newChannel;

void cursesSigwinch( int signum, void *ip )
{
}

void cursesLogWrite( char *line )
{
}

void cursesTextAdd( CursesWindow_t window, CursesTextAlign_t align, int x, 
                    int y, char *string )
{
}

void cursesKeyhandleRegister( CursesKeyhandleFunc_t func )
{
}

int cursesDetailsKeyhandle( int ch )
{
    return( 0 );
}

void cursesMenuItemRemove( int level, int menuId, char *string )
{
}

void cursesPluginDisplay( void *arg )
{
}

int cursesMenuItemAdd( int level, int menuId, char *string, 
                       CursesMenuFunc_t menuFunc, void *menuFuncArg )
{
    return 0;
}

void cursesChannelDisplay( void *arg )
{
}

void channelLeave( IRCServer_t *server, IRCChannel_t *channel, 
                   char *oldChannel )
{
}

int cursesMenuItemFind( int level, int menuId, char *string )
{
    return 0;
}

void cursesMenuSetIndex( int menuId, int index )
{
}

void cursesServerDisplay( void *arg )
{
}

void cursesCancel( void *arg, char *string )
{
}

void serverKill( BalancedBTreeItem_t *node, IRCServer_t *server, bool unalloc )
{
}

void regexpBotCmdAdd( IRCServer_t *server, IRCChannel_t *channel )
{
}

void versionAdd( char *what, char *version )
{
}

void versionRemove( char *what )
{
}

void BN_ExtractNick(const char *blah, char blah2[], int blah3)
{
}

IRCChannel_t *FindChannelNum( IRCServer_t *server, int channum )
{
    return NULL;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
