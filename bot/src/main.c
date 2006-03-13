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
#include "botnet.h"
#include "protos.h"
#include "release.h"


static char ident[] _UNUSED_= 
    "$Id$";

char   *mysql_host;
uint16  mysql_portnum;
char   *mysql_user;
char   *mysql_password;
char   *mysql_db;
bool    verbose;
bool    Daemon;

void LogBanner( void );
void MainParseArgs( int argc, char **argv );
void MainDisplayUsage( char *program, char *errorMsg );

int main ( int argc, char **argv )
{
    /* Parse the command line options */
    MainParseArgs( argc, argv );

    /* Start up the Logging thread */
    logging_initialize();

    /* Print the startup log messages */
    LogBanner();

    /* Setup the MySQL connection */
    db_setup();
    db_check_schema();

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

    return(0);
}


void LogBanner( void )
{
    printf( "\nbeirdobot  (c) 2006 Gavin Hurlbut\n" );
    printf( "%s\n", svn_version() );
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
        {0, 0, 0, 0}
    };

    mysql_host = NULL;
    mysql_portnum = 0;
    mysql_user = NULL;
    mysql_password = NULL;
    mysql_db = NULL;
    verbose = false;
    Daemon = false;

    while( (opt = getopt_long( argc, argv, "hVH:P:u:p:d:Dv", longOpts, 
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
               "\t-V or --version\tshow the version number and quit\n"
               "\t-h or --help\tshow this help text\n\n" );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

