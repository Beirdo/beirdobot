/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  havokmud is free software; you can redistribute it and/or modify
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
 * Handles MySQL database connections
 */

#include "environment.h"
#include <stdio.h>
#include <string.h>
#include <mysql.h>
#include <stdlib.h>
#include "protos.h"

static char ident[] _UNUSED_ =
    "$Id$";

static MYSQL *sql;

static char sqlbuf[MAX_STRING_LENGTH] _UNUSED_;

/* Internal protos */
char *db_quote(char *string);


void db_setup(void)
{
    if( !(sql = mysql_init(NULL)) ) {
        fprintf(stderr, "Unable to initialize a MySQL structure!!\n");
        exit(1);
    }

    printf("Using database %s at %s:%d\n", mysql_db, mysql_host, mysql_portnum);

    if( !mysql_real_connect(sql, mysql_host, mysql_user, mysql_password, 
                            mysql_db, mysql_port, NULL, 0) ) {
        fprintf(stderr, "Unable to connect to the database\n");
        mysql_error(sql);
        exit(1);
    }
}

char *db_quote(char *string)
{
    int             len,
                    i,
                    j,
                    count;
    char           *retString;

    len = strlen(string);

    for(i = 0, count = 0; i < len; i++) {
        if( string[i] == '\'' || string[i] == '\"' ) {
            count++;
        }
    }

    if( !count ) {
        return( strdup(string) );
    }

    retString = (char *)malloc(len + count + 1);
    for(i = 0, j = 0; i < len; i++, j++) {
        if( string[i] == '\'' || string[i] == '\"' ) {
            retString[j++] = '\\';
        }
        retString[j] = string[i];
    }
    retString[j] = '\0';

    return( retString );
}


#if 0
struct index_data *db_generate_object_index(int *top, int *sort_top,
                                            int *alloc_top)
{
    struct index_data *index = NULL;
    int             i,
                    vnum,
                    j,
                    len;
    int             count,
                    keyCount;

    MYSQL_RES      *res,
                   *resKeywords;
    MYSQL_ROW       row;

    strcpy(sqlbuf, "SELECT `vnum` FROM `objects` WHERE `ownerId` = -1 AND "
                   "ownedItemId = -1 ORDER BY `vnum`" );
    mysql_query(sql, sqlbuf);

    res = mysql_store_result(sql);
    if( !res || !(count = mysql_num_rows(res)) ) {
        mysql_free_result(res);
        return( NULL );
    }

    index = (struct index_data *)malloc(count * sizeof(struct index_data));
    if( !index ) {
        mysql_free_result(res);
        return( NULL );
    }

    for( i = 0; i < count; i++ ) {
        row = mysql_fetch_row(res);

        vnum = atoi(row[0]);
        index[i].virtual = vnum;
        index[i].pos = -1;
        index[i].number = 0;
        index[i].data = NULL;
        index[i].func = NULL;

        sprintf( sqlbuf, "SELECT `keyword` FROM `objectKeywords` "
                         "WHERE `vnum` = %d AND `ownerId` = -1 AND "
                         "`ownedItemId` = -1 ORDER BY `seqNum`", vnum );
        mysql_query(sql, sqlbuf);

        index[i].name = NULL;
        len = 0;

        resKeywords = mysql_store_result(sql);
        if( !resKeywords || !(keyCount = mysql_num_rows(resKeywords)) ) {
            mysql_free_result(resKeywords);
            continue;
        }

        for( j = 0; j < keyCount; j++ ) {
            row = mysql_fetch_row(resKeywords);

            index[i].name = (char *)realloc(index[i].name, 
                                            len + strlen(row[0]) + 2);
            if( !len ) {
                strcpy( index[i].name, row[0] );
            } else {
                strcat( index[i].name, " " );
                strcat( index[i].name, row[0] );
            }
            len = strlen(index[i].name);
        }

        mysql_free_result(resKeywords);
    }
    mysql_free_result(res);

    *sort_top = count - 1;
    *alloc_top = count;
    *top = count;

    return (index);
}
#endif


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
