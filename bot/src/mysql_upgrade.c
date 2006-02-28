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
 * Handles MySQL database schema upgrades
 */

#include <stdio.h>
#include <string.h>
#include <mysql.h>
#include <stdlib.h>
#include "botnet.h"
#include "environment.h"
#include "structs.h"
#include "protos.h"
#include "db_schema.h"

static char ident[] _UNUSED_ =
    "$Id$";

#define MAX_SCHEMA_QUERY 100
typedef char *SchemaUpgrade_t[MAX_SCHEMA_QUERY];

SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA] = {
    /* 0 -> 1 */
    { NULL },
    /* 1 -> 2 */
    {
        "CREATE TABLE `nickhistory` (\n"
        "  `chanid` int(11) NOT NULL default '0',\n"
        "  `nick` varchar(64) NOT NULL default '',\n"
        "  `histType` int(11) NOT NULL default '0',\n"
        "  `timestamp` int(11) NOT NULL default '0',\n"
        "  PRIMARY KEY  (`chanid`,`nick`,`histType`,`timestamp`)\n"
        ") TYPE=MyISAM;\n",
        NULL
    }
};

/* Internal protos */
extern MYSQL_RES *db_query( char *format, ... );
int db_upgrade_schema( int current, int goal );


void db_check_schema(void)
{
    char               *verString;
    int                 ver;
    int                 printed;

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting("dbSchema");

        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }
        if( ver < CURRENT_SCHEMA ) {
            if( !printed ) {
                fprintf( stderr, "Current database schema version %d, code "
                                 "supports version %d\n", ver, CURRENT_SCHEMA );
                printed = TRUE;
            }
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA );
        }
    } while( ver < CURRENT_SCHEMA );
}

int db_upgrade_schema( int current, int goal )
{
    int                 i;
    MYSQL_RES          *res;

    if( current >= goal ) {
        return( current );
    }

    if( current == 0 ) {
        /* There is no dbSchema, assume that it is an empty database, populate
         * with the default schema
         */
        fprintf( stderr, "Initializing database to schema version %d\n",
                 CURRENT_SCHEMA );
        for( i = 0; i < defSchemaCount; i++ ) {
            res = db_query( defSchema[i] );
            mysql_free_result(res);
        }
        db_set_setting("dbSchema", "%d", CURRENT_SCHEMA);
        return( CURRENT_SCHEMA );
    }

    fprintf( stderr, "Upgrading database from schema version %d to %d\n",
             current, current+1 );
    for( i = 0; schemaUpgrade[current][i]; i++ ) {
        res = db_query( schemaUpgrade[current][i] );
        mysql_free_result(res);
    }

    current++;

    db_set_setting("dbSchema", "%d", current);
    return( current );
}



/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
