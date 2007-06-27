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
#include <execinfo.h>
#include <ucontext.h>
#include <curl/curl.h>
#include "botnet.h"
#include "protos.h"
#include "release.h"
#include "queue.h"
#include "logging.h"


static char ident[] _UNUSED_= 
    "$Id$";

char       *mysql_host;
uint16      mysql_portnum;
char       *mysql_user;
char       *mysql_password;
char       *mysql_db;
bool        verbose;
bool        Daemon;
bool        Debug;
bool        GlobalAbort;
bool        BotDone;
pthread_t   mainThreadId;

void mainSighup( int signum, void *arg );
void LogBanner( void );
void MainParseArgs( int argc, char **argv );
void MainDisplayUsage( char *program, char *errorMsg );
void signal_interrupt( int signum, siginfo_t *info, void *secret );
void signal_everyone( int signum, siginfo_t *info, void *secret );
void signal_death( int signum, siginfo_t *info, void *secret );
void MainDelayExit( void );

typedef void (*sigAction_t)(int, siginfo_t *, void *);

int main ( int argc, char **argv )
{
    pthread_mutex_t     spinLockMutex;
    pid_t               childPid;
    struct sigaction    sa;
    sigset_t            sigmsk;

    GlobalAbort = false;

    /* Parse the command line options */
    MainParseArgs( argc, argv );

    /* Do we need to detach? */
    if( Daemon ) {
        childPid = fork();
        if( childPid < 0 ) {
            perror( "Couldn't detach in daemon mode" );
            _exit( 1 );
        }

        if( childPid != 0 ) {
            /* This is still the parent, report the child's pid and exit */
            printf( "[Detached as PID %d]\n", childPid );
            /* And exit the parent */
            _exit( 0 );
        }

        /* After this is in the detached child */

        /* Close stdin, stdout, stderr to release the tty */
        close(0);
        close(1);
        close(2);
    }

    mainThreadId = pthread_self();

    /* 
     * Setup the sigmasks for this thread (which is the parent to all others).
     * This will propogate to all children.
     */
    sigfillset( &sigmsk );
    sigdelset( &sigmsk, SIGUSR1 );
    sigdelset( &sigmsk, SIGUSR2 );
    sigdelset( &sigmsk, SIGHUP );
    sigdelset( &sigmsk, SIGINT );
    sigdelset( &sigmsk, SIGSEGV );
    sigdelset( &sigmsk, SIGILL );
    sigdelset( &sigmsk, SIGFPE );
    pthread_sigmask( SIG_SETMASK, &sigmsk, NULL );

    /* Initialize the non-threadsafe CURL library functionality */
    curl_global_init( CURL_GLOBAL_ALL );

    /* Start up the Logging thread */
    logging_initialize();

    thread_register( &mainThreadId, "thread_main", mainSighup, NULL );

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
    sigaction( SIGHUP, &sa, NULL );

    /* Setup signal handlers for SEGV, ILL, FPE */
    sa.sa_sigaction = signal_death;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction( SIGSEGV, &sa, NULL );
    sigaction( SIGILL, &sa, NULL );
    sigaction( SIGFPE, &sa, NULL );

    /* Print the startup log messages */
    LogBanner();

    /* Setup the MySQL connection */
    db_setup();
    db_check_schema_main();

    /* Setup the bot commands */
    botCmd_initialize();

    /* Setup the regexp support */
    regexp_initialize();

    /* Setup the plugins */
    plugins_initialize();

    /* Start the notifier thread */
    notify_start();

    /* Start the authenticate thread */
    authenticate_start();

    /* Start the bot */
    bot_start();

    /* Sit on this and rotate - this causes an intentional deadlock, this
     * thread should stop dead in its tracks
     */
    pthread_mutex_init( &spinLockMutex, NULL );
    pthread_mutex_lock( &spinLockMutex );
    pthread_mutex_lock( &spinLockMutex );

    return(0);
}


void LogBanner( void )
{
    LogPrintNoArg( LOG_CRIT, "beirdobot  (c) 2006 Gavin Hurlbut" );
    LogPrint( LOG_CRIT, "%s", svn_version() );
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
        {"daemon", 0, 0, 'D'},
        {"verbose", 0, 0, 'v'},
        {"debug", 0, 0, 'g'},
        {0, 0, 0, 0}
    };

    mysql_host = NULL;
    mysql_portnum = 0;
    mysql_user = NULL;
    mysql_password = NULL;
    mysql_db = NULL;
    verbose = false;
    Debug = false;
    Daemon = false;

    while( (opt = getopt_long( argc, argv, "hVH:P:u:p:d:Dgv", longOpts, 
                               &optIndex )) != -1 )
    {
        switch( opt )
        {
            case 'h':
                MainDisplayUsage( argv[0], NULL );
                exit( 0 );
                break;
            case 'D':
                Daemon = true;
                break;
            case 'g':
                Debug = true;
                break;
            case 'v':
                verbose = true;
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

    if( Daemon ) {
        verbose = false;
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
               "\t-D or --daemon\tRun solely in daemon mode, detached\n"
               "\t-v or --verbose\tShow verbose information while running\n"
               "\t-g or --debug\tWrite a debugging logfile\n"
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

    if( pthread_equal( myThreadId, mainThreadId ) ) {
        LogPrint( LOG_CRIT, "Received signal: %s", sys_siglist[signum] );

        ThreadAllKill( signum );

    }

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

void do_backtrace( int signum, void *ip )
{
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
}

void MainDelayExit( void )
{
    int         i;
    pthread_t   shutdownThreadId;

    LogPrintNoArg( LOG_CRIT, "Shutting down" );

    /* Signal to all that we are aborting */
    BotDone = false;

    GlobalAbort = true;

    /* Unload all plugins (which should kill all associated threads) */
    pluginUnloadAll();

    /* Send out signals from all queues waking up anything waiting on them so
     * the listeners can unblock and die
     */
    QueueKillAll();

    /* Shut down IRC connections */
    thread_create( &shutdownThreadId, bot_shutdown, NULL, "thread_shutdown",
                   NULL, NULL );

    /* Delay to allow all the other tasks to finish (esp. logging!) */
    for( i = 15; i && !BotDone; i-- ) {
        sleep(1);
    }

    LogPrintNoArg(LOG_DEBUG, "Shutdown complete!" );
    LogFlushOutput();

    /* And finally... die */
    _exit( 0 );
}

void mainSighup( int signum, void *arg )
{
    LogPrint( LOG_DEBUG, "Main received signal %d", signum );

    /*
     * Need to rescan the plugins
     */
    plugins_sighup();
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
