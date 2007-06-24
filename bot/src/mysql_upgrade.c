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


static SchemaUpgrade_t coreSchemaUpgrade[CURRENT_SCHEMA] = {
    /* 0 -> 1 */
    { { NULL, NULL, NULL, FALSE } },
    /* 1 -> 2 */
    {
      { "CREATE TABLE `nickhistory` (\n"
        "  `chanid` int(11) NOT NULL default '0',\n"
        "  `nick` varchar(64) NOT NULL default '',\n"
        "  `histType` int(11) NOT NULL default '0',\n"
        "  `timestamp` int(11) NOT NULL default '0',\n"
        "  PRIMARY KEY  (`chanid`,`nick`,`histType`,`timestamp`)\n"
        ") TYPE=MyISAM\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 2 -> 3 */
    {
      { "ALTER TABLE `nickhistory` DROP PRIMARY KEY ,\n"
        "ADD PRIMARY KEY ( `chanid` , `histType` , `timestamp` , `nick` )\n",
        NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 3 -> 4 */
    {
      { "ALTER TABLE `channels` ADD `notify` INT DEFAULT '0' NOT NULL AFTER "
        "`channel`\n", NULL, NULL, FALSE },
      { "UPDATE `channels` SET `notify` = '1' WHERE `url` != ''\n", NULL, NULL,
        FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 4 -> 5 */
    {
      { "ALTER TABLE `irclog` DROP INDEX `timeChan` ,\n"
        "ADD INDEX `timeChan` ( `chanid` , `timestamp` )\n", NULL, NULL, 
        FALSE },
      { "ALTER TABLE `irclog` DROP INDEX `messageType` ,\n"
        "ADD INDEX `messageType` ( `msgtype` , `chanid` , `timestamp` )\n",
        NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 5 -> 6 */
    {
      { "CREATE TABLE `userauth` (\n"
        " `username` VARCHAR( 64 ) NOT NULL ,\n"
        " `digest` ENUM( 'md4', 'md5', 'sha1' ) NOT NULL ,\n"
        " `seed` VARCHAR( 20 ) NOT NULL ,\n"
        " `key` VARCHAR( 16 ) NOT NULL ,\n"
        " `keyIndex` INT NOT NULL ,\n"
        " PRIMARY KEY ( `username` )\n"
        ") TYPE = MYISAM\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 6 -> 7 */
    {
      { NULL, NULL, NULL, FALSE }
    },
    /* 7 -> 8 */
    {
      { "ALTER TABLE `servers` ADD `password` VARCHAR( 255 ) NOT NULL "
        "AFTER `port`\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 8 -> 9 */
    {
      { "ALTER TABLE `servers` ADD `floodInterval` INT DEFAULT '2' NOT NULL ,\n"
        "ADD `floodMaxTime` INT DEFAULT '8' NOT NULL ,\n"
        "ADD `floodBuffer` INT DEFAULT '2000' NOT NULL ,\n"
        "ADD `floodMaxLine` INT DEFAULT '256' NOT NULL\n", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    },
    /* 9 -> 10 */
    {
      { "ALTER TABLE `channels` ADD `enabled` INT DEFAULT '1' NOT NULL "
        "AFTER `serverid`", NULL, NULL, FALSE },
      { "ALTER TABLE `servers` ADD `enabled` INT DEFAULT '1' NOT NULL "
        "AFTER `serverid`", NULL, NULL, FALSE },
      { NULL, NULL, NULL, FALSE }
    }
};

/* Internal protos */
extern void db_queue_query( int queryId, QueryTable_t *queryTable,
                            MYSQL_BIND *queryData, int queryDataCount,
                            QueryResFunc_t queryCallback, 
                            void *queryCallbackArg,
                            pthread_mutex_t *queryMutex );
static int db_upgrade_schema( char *setting, char *desc, 
                              QueryTable_t *defSchema, int defSchemaCount,
                              SchemaUpgrade_t *schemaUpgrade, int current, 
                              int codeSupports );

void db_check_schema_main( void )
{
    db_check_schema( "dbSchema", "core", CURRENT_SCHEMA, defCoreSchema,
                     defCoreSchemaCount, coreSchemaUpgrade );
}

void db_check_schema( char *setting, char *desc, int codeSupports, 
                      QueryTable_t *defSchema, int defSchemaCount, 
                      SchemaUpgrade_t *schemaUpgrade )
{
    char               *verString;
    int                 ver;
    int                 printed;

    ver = -1;
    printed = FALSE;
    do {
        verString = db_get_setting( setting );
        if( !verString ) {
            ver = 0;
        } else {
            ver = atoi( verString );
            free( verString );
        }

        if( !printed ) {
            LogPrint( LOG_CRIT, "Current %s database schema version %d", desc,
                                ver );
            LogPrint( LOG_CRIT, "Code supports version %d", codeSupports );
            printed = TRUE;
        }

        if( ver < codeSupports ) {
            ver = db_upgrade_schema( setting, desc, defSchema, defSchemaCount,
                                     schemaUpgrade, ver, codeSupports );
        }
    } while( ver < codeSupports );
}

static int db_upgrade_schema( char *setting, char *desc, 
                              QueryTable_t *defSchema, int defSchemaCount,
                              SchemaUpgrade_t *schemaUpgrade, int current, 
                              int codeSupports )
{
    int                 i;

    if( current >= codeSupports ) {
        return( current );
    }

    if( current == 0 ) {
        /* There is no dbSchema, assume that it is an empty database, populate
         * with the default schema
         */
        LogPrint( LOG_ERR, "Initializing %s database to schema version %d",
                  desc, codeSupports );
        for( i = 0; i < defSchemaCount; i++ ) {
            db_queue_query( i, defSchema, NULL, 0, NULL, NULL, NULL );
        }
        db_set_setting( setting, "%d", codeSupports );
        return( codeSupports );
    }

    LogPrint( LOG_ERR, "Upgrading %s database from schema version %d to %d",
                       desc, current, current+1 );
    for( i = 0; schemaUpgrade[current][i].queryPattern; i++ ) {
        db_queue_query( i, schemaUpgrade[current], NULL, 0, NULL, NULL, NULL );
    }

    current++;

    db_set_setting( setting, "%d", current );
    return( current );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
