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

/*
 * HEADER--------------------------------------------------- 
 * $Id$ 
 *
 * Copyright 2006 Gavin Hurlbut 
 * All rights reserved 
 * 
 */

#include "environment.h"
#include "botnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opie.h>
#include "protos.h"
#include "structs.h"


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static int changed(AuthData_t *auth, char *nick);

extern int __opieparsechallenge(char *buffer, int *algorithm, int *sequence, 
                                char **seed, int *exts);


#define RESPONSE_STANDARD  0
#define RESPONSE_WORD      1
#define RESPONSE_HEX       2
#define RESPONSE_INIT_HEX  3
#define RESPONSE_INIT_WORD 4
#define RESPONSE_UNKNOWN   5

struct _rtrans {
  int type;
  char *name;
};

static struct _rtrans rtrans[] = {
  { RESPONSE_WORD, "word" },
  { RESPONSE_HEX, "hex" },
  { RESPONSE_INIT_HEX, "init-hex" },
  { RESPONSE_INIT_WORD, "init-word" },
  { RESPONSE_STANDARD, "" },
  { RESPONSE_UNKNOWN, NULL }
};

static char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };


char *auth_user_challenge( char *nick )
{
    AuthData_t         *auth;
    char               *challenge;

    challenge = (char *)malloc(256);
    auth = db_get_auth( nick );
    if( !auth ) {
        opierandomchallenge( challenge );
    } else {
        sprintf(challenge, "otp-%s %d %s ext", auth->digest, auth->count - 1,
                auth->seed);
    }
    db_free_auth( auth );

    return( challenge );
}


bool auth_user_verify( char *nick, char *response )
{
    int             i,
                    j,
                    k;
    char           *c;
    char           *c2;
    char            key[8],
                    fkey[8],
                    lastkey[8];
    struct _rtrans *r;
    AuthData_t     *auth;
    int             alg;

    if (!nick || !response) {
        return( false );
    }

    auth = db_get_auth( nick );
    if( !auth ) {
        return( false );
    }

    for( alg = -1, i = 0; i == 0 && alg < (int)(NELEMENTS(algids) - 1); 
         alg++ ) {
        if( algids[alg+1] && !strcasecmp(algids[alg+1], auth->digest ) ) {
            i = 1;
        }
    }

    if( !i ) {
        db_free_auth( auth );
        return( false );
    }
            
    if (!opieatob8(lastkey, auth->hash)) {
        db_free_auth(auth);
        return( false );
    }

    if ((c = strchr(response, ':'))) {
        *(c++) = 0;
        for (r = rtrans; r->name && strcmp(r->name, response); r++);
        i = r->type;
    } else {
        i = RESPONSE_STANDARD;
    }

    switch (i) {
    case RESPONSE_STANDARD:
        i = 1;

        if (opieetob(key, response) == 1) {
            memcpy(fkey, key, sizeof(key));
            opiehash(fkey, alg);
            i = memcmp(fkey, lastkey, sizeof(key));
        }
        if (i && opieatob8(key, response)) {
            memcpy(fkey, key, sizeof(key));
            opiehash(fkey, alg);
            i = memcmp(fkey, lastkey, sizeof(key));
        }
        break;
    case RESPONSE_WORD:
        i = 1;

        if (opieetob(key, c) == 1) {
            memcpy(fkey, key, sizeof(key));
            opiehash(fkey, alg);
            i = memcmp(fkey, lastkey, sizeof(key));
        }
        break;
    case RESPONSE_HEX:
        i = 1;

        if (opieatob8(key, c)) {
            memcpy(fkey, key, sizeof(key));
            opiehash(fkey, alg);
            i = memcmp(fkey, lastkey, sizeof(key));
        }
        break;
    case RESPONSE_INIT_HEX:
    case RESPONSE_INIT_WORD:
        if (!(c2 = strchr(c, ':'))) {
            db_free_auth( auth );
            return( false );
        }

        *(c2++) = 0;

        if (i == RESPONSE_INIT_HEX) {
            if (!opieatob8(key, c)) {
                db_free_auth( auth );
                return( false );
            }
        } else {
            if (opieetob(key, c) != 1) {
                db_free_auth( auth );
                return( false );
            }
        }

        memcpy(fkey, key, sizeof(key));
        opiehash(fkey, alg);

        if (memcmp(fkey, lastkey, sizeof(key))) {
            db_free_auth( auth );
            return( false );
        }

        if (changed(auth, nick)) {
            db_free_auth( auth );
            return( false );
        }

        auth->count--;

        if (!opiebtoa8(auth->hash, key)) {
            db_free_auth( auth );
            return( false );
        }

        db_set_auth(nick, auth);

        if (!(c2 = strchr(c = c2, ':'))) {
            db_free_auth( auth );
            return( false );
        }

        *(c2++) = 0;

        if (__opieparsechallenge(c, &j, &(auth->count), &(auth->seed), &k) ||
            (j != alg) || k) {
            db_free_auth( auth );
            return( false );
        }

        if (i == RESPONSE_INIT_HEX) {
            if (!opieatob8(key, c2)) {
                db_free_auth( auth );
                return( false );
            }
        } else {
            if (opieetob(key, c2) != 1) {
                db_free_auth( auth );
                return( false );
            }
        }
        goto verwrt;
    case RESPONSE_UNKNOWN:
    default:
        db_free_auth( auth );
        return( false );
        break;
    }

    if (i) {
        db_free_auth( auth );
        return( false );
    }

    if (changed(auth, nick)) {
        db_free_auth( auth );
        return( false );
    }

    auth->count--;

  verwrt:
    if (!opiebtoa8(auth->hash, key)) {
        db_free_auth( auth );
        return( false );
    }

    db_set_auth( nick, auth );
    db_free_auth( auth );
    return( true );
}


static int changed(AuthData_t *auth, char *nick)
{
    AuthData_t         *newAuth;

    newAuth = db_get_auth( nick );
    if( !newAuth ) {
        return 1;
    }

    if( newAuth->count != auth->count || strcmp(newAuth->hash, auth->hash) ||
        strcmp(newAuth->seed, auth->seed) ) {
        return 1;
    }

    db_free_auth( newAuth );

    return 0;
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
