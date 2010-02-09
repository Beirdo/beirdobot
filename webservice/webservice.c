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

#define ___ARGH
#include "environment.h"
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <execinfo.h>
#include <ucontext.h>
#include <assert.h>
#include "release.h"
#include "clucene.h"
#include "mongoose.h"


static char ident[] _UNUSED_= 
    "$Id$";

bool                Daemon = TRUE;
bool                Debug = FALSE;
int                 portnum = 32123;

void LogBanner( void );
void MainParseArgs( int argc, char **argv );
void MainDisplayUsage( char *program, char *errorMsg );
void signal_death( int signum, siginfo_t *info, void *secret );
void signal_child( int signum, siginfo_t *info, void *secret );
void do_symbol( void *ptr );
void do_backtrace( int signum, void *ip );
void show_search(struct mg_connection *conn, 
                 const struct mg_request_info *request_info, void *user_data);

typedef void (*sigAction_t)(int, siginfo_t *, void *);

int main ( int argc, char **argv )
{
    struct sigaction        sa;
	struct mg_context	   *ctx;
    pid_t                   childPid;
    char                    port[16];
    char                    buf[1024];

    /* Parse the command line options */
    MainParseArgs( argc, argv );

    /* Setup signal handlers for SEGV, ILL, FPE, INT */
    sa.sa_sigaction = signal_death;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction( SIGSEGV, &sa, NULL );
    sigaction( SIGILL, &sa, NULL );
    sigaction( SIGFPE, &sa, NULL );
    sigaction( SIGINT, &sa, NULL );

    /* Setup signal handler for CHLD */
    sa.sa_sigaction = signal_child;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction( SIGCHLD, &sa, NULL );

    /* Setup signal handler for PIPE */
    sa.sa_handler = SIG_IGN;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART;
    sigaction( SIGPIPE, &sa, NULL );

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
    } else {
        /* Print the startup log messages */
        LogBanner();
    }

    sprintf( port, "%d", portnum );

	/*
	 * Initialize mongoose context.
	 * Set WWW root to current directory.
	 * Start listening on port specified.
	 */
	ctx = mg_start();
	mg_set_option(ctx, "ports", port);

    sprintf( buf, "%s/access.log", LOG_DIR );
    mg_set_option(ctx, "access_log", buf);

    sprintf( buf, "%s/error.log", LOG_DIR );
    mg_set_option(ctx, "error_log", buf);

	/* Register an search URL */
	mg_set_uri_callback(ctx, "/search", &show_search, NULL);

    while(1) {
        sleep(1);
    }

    mg_stop(ctx);

    return(0);
}

void LogBanner( void )
{
    printf( "webserviced from beirdobot  (c) 2010 Gavin Hurlbut\n" );
    printf( "%s\n", git_version() );
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
        {"port", 1, 0, 'P'},
        {"verbose", 0, 0, 'v'},
        {0, 0, 0, 0}
    };

    while( (opt = getopt_long( argc, argv, "hVvP:", longOpts, 
                               &optIndex )) != -1 )
    {
        switch( opt )
        {
            case 'h':
                MainDisplayUsage( argv[0], NULL );
                exit( 0 );
                break;
            case 'P':
                portnum = atoi(optarg);
                break;
            case 'V':
                LogBanner();
                exit( 0 );
                break;
            case 'v':
                Daemon = FALSE;        
                break;
            case '?':
            case ':':
            default:
                MainDisplayUsage( argv[0], "Unknown option" );
                exit( 1 );
                break;
        }
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

    fprintf( stderr, "\nUsage:\n\t%s [-P port]\n\n", program );
    fprintf( stderr, 
               "Options:\n"
               "\t-P or --port\tTCP port to accept connections on (default 32123)\n"
               "\t-V or --version\tshow the version number and quit\n"
               "\t-h or --help\tshow this help text\n\n" );
}

#ifdef REG_EIP
 #define OLD_IP REG_EIP
#else
 #ifdef REG_RIP
  #define OLD_IP REG_RIP
 #endif
#endif

void signal_death( int signum, siginfo_t *info, void *secret )
{
    extern const char *const    sys_siglist[];
    ucontext_t                 *uc;
    struct sigaction            sa;

    if( signum != SIGINT && !Daemon ) {
        uc = (ucontext_t *)secret;

        /* Make it so another bad signal will just KILL it */
        sa.sa_handler = SIG_DFL;
        sigemptyset( &sa.sa_mask );
        sa.sa_flags = SA_RESTART;
        sigaction( SIGSEGV, &sa, NULL );
        sigaction( SIGILL, &sa, NULL );
        sigaction( SIGFPE, &sa, NULL );

        printf( "Received signal: %s\n", sys_siglist[signum] );
#ifdef OLD_IP
        printf( "Faulty Address: %p, from %p\n", info->si_addr,
                            (void *)uc->uc_mcontext.gregs[OLD_IP] );
#else
        printf( "Faulty Address %p, no discernable context\n", info->si_addr );
#endif

#ifdef OLD_IP
        do_backtrace( signum, (void *)uc->uc_mcontext.gregs[OLD_IP] );
#else
        do_backtrace( signum, NULL );
#endif
    }

    /* Kill this thing HARD! */
    _exit(0);
}

void do_symbol( void *ptr )
{
    void               *array[1];
    char              **strings;

    array[0] = ptr;
    strings = backtrace_symbols( array, 1 );

    printf( "%s\n", strings[0] );

    free( strings );
}

void do_backtrace( int signum, void *ip )
{
    void               *array[100];
    size_t              size;
    char              **strings;
    size_t              i;

    size = backtrace( array, 100 );

#if 0
    /* replace the sigaction/pthread_kill with the caller's address */
    if( ip ) {
        array[1] = ip;
    }
#endif

    strings = backtrace_symbols( array, size );

    printf( "Obtained %zd stack frames.\n", size );

    for( i = 0; i < size; i++ ) {
        printf( "%s\n", strings[i] );
    }

    free( strings );
}

/*
 * Make sure we have ho zombies from CGIs
 */
void signal_child( int signum, siginfo_t *info, void *secret )
{
    while (waitpid(-1, &signum, WNOHANG) > 0) ;
}

/*
 * This callback is attached to the URI "/search"
 */
void show_search(struct mg_connection *conn, 
                 const struct mg_request_info *request_info, void *user_data)
{

    SearchResults_t    *results;
    int                 count;
    int                 i;
    time_t              time_start, time_end;
    struct timeval      tv_start, tv_end, tv_elapsed;
    float               score;
    char               *string, *channum, *max;
    int                 chanid; 
    int                 maxcount = 20;

    string  = mg_get_var( conn, "s" );
    channum = mg_get_var( conn, "c" );
    max = mg_get_var( conn, "max" );

    if( string == NULL || channum == NULL ) {
        if( string != NULL ) {
            mg_free(string);
        }
        if( channum != NULL ) {
            mg_free(channum);
        }
        mg_printf(conn, "HTTP/1.0 400 Bad Request\n\n" );
        return;
    }

    if( max != NULL ) {
        maxcount = atoi(max);
        mg_free(max);
    }

    if( maxcount < 0 ) {
        maxcount = 1;
    }

    if( maxcount > 100 ) {
        maxcount = 100;
    }

    chanid = atoi(channum);
    mg_free( channum );

	mg_printf(conn, "HTTP/1.0 200 OK\nContent-Type: application/json\n\n");
    mg_printf(conn, "{ \"searchString\": \"%s\", \"searchChannel\": %d, "
                    "\"results\": [ ", string, chanid );

    gettimeofday(&tv_start, NULL);
    results = clucene_search( chanid, string, &count, maxcount );
    gettimeofday(&tv_end, NULL);
    timersub(&tv_end, &tv_start, &tv_elapsed);

    mg_free( string );

    if( !results ) {
        mg_printf( conn, "], \"resultCount\": 0, \"searchTime\": %d.%06d }\n\n",
                   tv_elapsed.tv_sec, tv_elapsed.tv_usec );
        return;
    }

    for( i = 0; i < count; i++ ) {
        time_start = results[i].timestamp;
        time_end   = time_start + SEARCH_WINDOW;
        score = results[i].score;

        mg_printf( conn, "{ \"startTime\": %d, \"endTime\": %d, "
                         "\"score\": %.2f }", time_start, time_end, score );
        if( i < count-1 ) {
            mg_printf( conn, ", " );
        }
    }

    mg_printf( conn, " ], \"resultCount\": %d, \"searchTime\": %d.%06d }\n\n", 
               count, tv_elapsed.tv_sec, tv_elapsed.tv_usec );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
