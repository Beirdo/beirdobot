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
#include "logging.h"

static char ident[] _UNUSED_ =
    "$Id$";

#define MAX_SCHEMA_QUERY 100
typedef char *SchemaUpgrade_t[MAX_SCHEMA_QUERY];

static SchemaUpgrade_t schemaUpgrade[CURRENT_SCHEMA] = {
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
        ") TYPE=MyISAM\n",
        NULL
    },
    /* 2 -> 3 */
    {
        "ALTER TABLE `nickhistory` DROP PRIMARY KEY ,\n"
        "ADD PRIMARY KEY ( `chanid` , `histType` , `timestamp` , `nick` )\n",
        NULL
    },
    /* 3 -> 4 */
    {
        "ALTER TABLE `channels` ADD `notify` INT DEFAULT '0' NOT NULL AFTER "
        "`channel`\n",
        "UPDATE `channels` SET `notify` = '1' WHERE `url` != ''\n",
        NULL
    },
    /* 4 -> 5 */
    {
        "ALTER TABLE `irclog` DROP INDEX `timeChan` ,\n"
        "ADD INDEX `timeChan` ( `chanid` , `timestamp` )\n",
        "ALTER TABLE `irclog` DROP INDEX `messageType` ,\n"
        "ADD INDEX `messageType` ( `msgtype` , `chanid` , `timestamp` )\n",
        NULL
    },
    /* 5 -> 6 */
    {
        "CREATE TABLE `userauth` (\n"
        " `username` VARCHAR( 64 ) NOT NULL ,\n"
        " `digest` ENUM( 'md4', 'md5', 'sha1' ) NOT NULL ,\n"
        " `seed` VARCHAR( 20 ) NOT NULL ,\n"
        " `key` VARCHAR( 16 ) NOT NULL ,\n"
        " `keyIndex` INT NOT NULL ,\n"
        " PRIMARY KEY ( `username` )\n"
        ") TYPE = MYISAM\n",
        NULL
    },
    /* 6 -> 7 */
    {
        "CREATE TABLE `plugin_trac` (\n"
        " `chanid` INT NOT NULL ,\n"
        " `url` VARCHAR( 255 ) NOT NULL ,\n"
        " PRIMARY KEY ( `chanid` )\n"
        ") TYPE = MYISAM\n",
        NULL
    },
    /* 7 -> 8 */
    {
        "ALTER TABLE `servers` ADD `password` VARCHAR( 255 ) NOT NULL "
        "AFTER `port`\n",
        NULL
    },
    /* 8 -> 9 */
    {
        "ALTER TABLE `servers` ADD `floodInterval` INT DEFAULT '2' NOT NULL ,\n"
        "ADD `floodMaxTime` INT DEFAULT '8' NOT NULL ,\n"
        "ADD `floodBuffer` INT DEFAULT '2000' NOT NULL ,\n"
        "ADD `floodMaxLine` INT DEFAULT '256' NOT NULL\n",
        NULL
    }
};

/* Internal protos */
extern MYSQL_RES *db_query( char *format, ... );
static int db_upgrade_schema( int current, int goal );


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

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current database schema version %d", ver );
            LogPrint( LOG_CRIT, "Code supports version %d", CURRENT_SCHEMA );
            printed = TRUE;
        }

        if( ver < CURRENT_SCHEMA ) {
            ver = db_upgrade_schema( ver, CURRENT_SCHEMA );
        }
    } while( ver < CURRENT_SCHEMA );
}

static int db_upgrade_schema( int current, int goal )
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
        LogPrint( LOG_ERR, "Initializing database to schema version %d",
                  CURRENT_SCHEMA );
        for( i = 0; i < defSchemaCount; i++ ) {
            res = db_query( defSchema[i] );
            mysql_free_result(res);
        }
        db_set_setting("dbSchema", "%d", CURRENT_SCHEMA);
        return( CURRENT_SCHEMA );
    }

    LogPrint( LOG_ERR, "Upgrading database from schema version %d to %d",
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
