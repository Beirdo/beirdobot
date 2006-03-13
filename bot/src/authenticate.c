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
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "protos.h"
#include "structs.h"
#include "logging.h"


/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

static int changed(AuthData_t *auth, char *nick);

extern int __opieparsechallenge(char *buffer, int *algorithm, int *sequence, 
                                char **seed, int *exts);

void *authenticate_thread(void *arg);
char *auth_user_challenge( AuthData_t **pAuth, char *nick );
bool auth_user_verify( AuthData_t **pAuth, char *nick, char *response );


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

LinkedList_t   *AuthList;
pthread_t       authentThreadId;

char *auth_user_challenge( AuthData_t **pAuth, char *nick )
{
    char               *challenge;
    AuthData_t         *auth;

    challenge = (char *)malloc(256);
    auth = db_get_auth( nick );
    if( !auth ) {
        opierandomchallenge( challenge );
    } else {
        sprintf(challenge, "otp-%s %d %s ext", auth->digest, auth->count - 1,
                auth->seed);
    }
    *pAuth = auth;

    return( challenge );
}


bool auth_user_verify( AuthData_t **pAuth, char *nick, char *response )
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

    auth = *pAuth;
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
        return( false );
    }
            
    if (!opieatob8(lastkey, auth->hash)) {
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
            return( false );
        }

        *(c2++) = 0;

        if (i == RESPONSE_INIT_HEX) {
            if (!opieatob8(key, c)) {
                return( false );
            }
        } else {
            if (opieetob(key, c) != 1) {
                return( false );
            }
        }

        memcpy(fkey, key, sizeof(key));
        opiehash(fkey, alg);

        if (memcmp(fkey, lastkey, sizeof(key))) {
            return( false );
        }

        if (changed(auth, nick)) {
            return( false );
        }

        auth->count--;

        if (!opiebtoa8(auth->hash, key)) {
            return( false );
        }

        db_set_auth(nick, auth);
        *pAuth = auth;

        if (!(c2 = strchr(c = c2, ':'))) {
            return( false );
        }

        *(c2++) = 0;

        if (__opieparsechallenge(c, &j, &(auth->count), &(auth->seed), &k) ||
            (j != alg) || k) {
            return( false );
        }

        if (i == RESPONSE_INIT_HEX) {
            if (!opieatob8(key, c2)) {
                return( false );
            }
        } else {
            if (opieetob(key, c2) != 1) {
                return( false );
            }
        }
        goto verwrt;
    case RESPONSE_UNKNOWN:
    default:
        return( false );
        break;
    }

    if (i) {
        return( false );
    }

    if (changed(auth, nick)) {
        return( false );
    }

    auth->count--;

  verwrt:
    if (!opiebtoa8(auth->hash, key)) {
        return( false );
    }

    db_set_auth( nick, auth );
    *pAuth = auth;

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


void authenticate_start(void)
{
    pthread_create( &authentThreadId, NULL, authenticate_thread, NULL );
}

void *authenticate_thread(void *arg)
{
    LinkedListItem_t   *item;
    LinkedListItem_t   *prev;
    AuthData_t         *auth;
    struct timespec     ts;
    struct timeval      now;
    char                timedout[] = "Authentication timed out";
    static char        *command = "authenticate";

    AuthList = LinkedListCreate();

    ts.tv_sec = 1;
    ts.tv_nsec = 0L;

    botCmd_add( (const char **)&command, authenticate_state_machine, NULL );

    LogPrintNoArg(LOG_NOTICE, "Starting authenticate thread");

    while( true ) {
        gettimeofday( &now, NULL );
        LinkedListLock( AuthList );
        for( item = AuthList->head, prev = NULL; item; 
             item = (prev == NULL ? AuthList->head : item->next) ) {
            auth = (AuthData_t *)item;

            if( auth->wakeTime != 0 && auth->wakeTime <= now.tv_sec ) {
                auth->state = AUTH_TIMEDOUT;
                BN_SendPrivateMessage( &auth->server->ircInfo, 
                                       (const char *)auth->nick, timedout );
                LogPrint( LOG_NOTICE, "Authentication timeout for %s", 
                          auth->nick );
            }

            if( auth->state == AUTH_REJECTED || 
                auth->state == AUTH_TIMEDOUT ||
                auth->state == AUTH_DISCONNECT ) {
                prev = item->prev;
                LinkedListRemove( AuthList, item, LOCKED );
                db_free_auth( auth );
                item = prev;
            } else {
                prev = item;
            }
        }
        LinkedListUnlock( AuthList );

        nanosleep( &ts, NULL );
    }

    return(NULL);
}

bool authenticate_check( IRCServer_t *server, char *nick )
{
    LinkedListItem_t   *item;
    AuthData_t         *auth = NULL;
    bool                found;

    LinkedListLock( AuthList );
    for( item = AuthList->head, found = false; item && !found; 
         item = item->next ) {
        auth = (AuthData_t *)item;

        if( auth->server == server && !strcasecmp(nick, auth->nick) ) {
            found = true;
        }
    }

    LinkedListUnlock( AuthList );

    if( !found || !auth || auth->state != AUTH_ACCEPTED ) {
        return( false );
    } else {
        return( true );
    }
}


void authenticate_state_machine( IRCServer_t *server, IRCChannel_t *channel,
                                 char *nick, char *msg )
{
    LinkedListItem_t   *item;
    AuthData_t         *auth;
    struct timeval      now;
    bool                found;
    char               *string;

    if( channel ) {
        return;
    }

    LinkedListLock( AuthList );
    for( item = AuthList->head, found = false; item && !found; 
         item = item->next ) {
        auth = (AuthData_t *)item;

        if( auth->server == server && !strcasecmp(nick, auth->nick) ) {
            found = true;
        }
    }

    LinkedListUnlock( AuthList );

    gettimeofday( &now, NULL );

    if( !found ) {
        string = auth_user_challenge( &auth, nick );
        if( auth ) {
            auth->server = server;
            auth->state = AUTH_CHALLENGE;
            auth->wakeTime = now.tv_sec + 30;
            LinkedListAdd( AuthList, (LinkedListItem_t *)auth, UNLOCKED, 
                           AT_HEAD );
        }

        BN_SendPrivateMessage( &server->ircInfo, (const char *)nick, string );
        free( string );
        return;
    }

    switch( auth->state ) {
    case AUTH_CHALLENGE:
        if( auth_user_verify( &auth, nick, msg ) ) {
            auth->state = AUTH_ACCEPTED;
            auth->wakeTime = now.tv_sec + (30 * 60);
            string = strdup( "Authentication accepted for 30min" );
            LogPrint( LOG_NOTICE, "Authenticated accepted for %s", nick );
        } else {
            auth->state = AUTH_REJECTED;
            auth->wakeTime = now.tv_sec;
            string = strdup( "Authentication rejected" );
            LogPrint( LOG_NOTICE, "Authenticated rejected for %s", nick );
        }

        BN_SendPrivateMessage( &server->ircInfo, (const char *)nick, string );
        free( string );
        break;
    case AUTH_ACCEPTED:
        if( !strcasecmp(msg, "logoff") ) {
            auth->state = AUTH_DISCONNECT;
            auth->wakeTime = now.tv_sec + 5;
            string = strdup( "Logged off" );
            LogPrint( LOG_NOTICE, "Logoff by %s", nick );

            BN_SendPrivateMessage( &server->ircInfo, (const char *)nick, 
                                   string );
            free( string );
        }
        break;
    default:
        break;
    }
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
