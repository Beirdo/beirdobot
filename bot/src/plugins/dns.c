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
*/

/* INCLUDE FILES */
#include "environment.h"
#include "botnet.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include "structs.h"
#include "protos.h"
#include "queue.h"
#include "balanced_btree.h"
#include "logging.h"


/* INTERNAL FUNCTION PROTOTYPES */
void botCmdDig( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                char *msg, void *tag );
char *botHelpDig( void *tag );
char           *findRR(char *domain, int requested_type);
int skipToData(unsigned char *cp, unsigned short *type, unsigned short *class, 
               unsigned int *ttl, unsigned short *dlen, 
               unsigned char *endOfMsg);
int skipName(unsigned char *cp, unsigned char *endOfMsg);
char           *in_addr_arpa( char *dottedquad );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

pthread_t               dnsThreadId;
static pthread_mutex_t  shutdownMutex;
static bool             threadAbort = FALSE;
QueueObject_t          *DnsQ;
BalancedBTree_t        *dnsTypeTree;
BalancedBTree_t        *dnsTypeNumTree;

typedef struct {
    IRCServer_t        *server;
    IRCChannel_t       *channel;
    char               *nick;
    char               *command;
} DNSItem_t;

typedef struct {
    char               *name;
    int                 type;
} DNSType_t;

void *dns_thread(void *arg);

static DNSType_t        dnsTypes[] = {
    { "A",      ns_t_a },
    { "NS",     ns_t_ns },
    { "CNAME",  ns_t_cname },
    { "SOA",    ns_t_soa },
    { "PTR",    ns_t_ptr },
    { "MX",     ns_t_mx },
    { "TXT",    ns_t_txt }
};
static int dnsTypeCount = NELEMENTS(dnsTypes);

void plugin_initialize( char *args )
{
    static char            *command = "dig";
    int                     i;
    BalancedBTreeItem_t    *item;

    LogPrintNoArg( LOG_NOTICE, "Initializing dns..." );

    pthread_mutex_init( &shutdownMutex, NULL );
    thread_create( &dnsThreadId, dns_thread, NULL, "thread_dns" );

    dnsTypeTree = BalancedBTreeCreate( BTREE_KEY_STRING );
    dnsTypeNumTree = BalancedBTreeCreate( BTREE_KEY_INT );
    BalancedBTreeLock( dnsTypeTree );
    BalancedBTreeLock( dnsTypeNumTree );

    for( i = 0; i < dnsTypeCount; i++ ) {
        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( item ) {
            item->item = (void *)&dnsTypes[i];
            item->key  = (void *)&dnsTypes[i].name;
            BalancedBTreeAdd( dnsTypeTree, item, LOCKED, FALSE );
        }

        item = (BalancedBTreeItem_t *)malloc(sizeof(BalancedBTreeItem_t));
        if( item ) {
            item->item = (void *)&dnsTypes[i];
            item->key  = (void *)&dnsTypes[i].type;
            BalancedBTreeAdd( dnsTypeNumTree, item, LOCKED, FALSE );
        }
    }

    BalancedBTreeAdd( dnsTypeTree, NULL, LOCKED, TRUE );
    BalancedBTreeAdd( dnsTypeNumTree, NULL, LOCKED, TRUE );
    BalancedBTreeUnlock( dnsTypeTree );
    BalancedBTreeUnlock( dnsTypeNumTree );

    botCmd_add( (const char **)&command, botCmdDig, botHelpDig, NULL );
}

void plugin_shutdown( void )
{
    LogPrintNoArg( LOG_NOTICE, "Removing dns..." );
    botCmd_remove( "dig" );

    threadAbort = TRUE;

    pthread_mutex_lock( &shutdownMutex );
    pthread_mutex_destroy( &shutdownMutex );

    BalancedBTreeLock( dnsTypeTree );
    BalancedBTreeDestroy( dnsTypeTree );

    BalancedBTreeLock( dnsTypeNumTree );
    BalancedBTreeDestroy( dnsTypeNumTree );

    thread_deregister( dnsThreadId );
}

void *dns_thread(void *arg)
{
    BalancedBTreeItem_t    *item;
    DNSItem_t              *qItem;
    DNSType_t              *dnsType;
    char                   *dnsserver;
    char                   *type;
    char                   *host;
    char                   *line;
    char                   *command;
    char                   *response;
    static char            *type_ptr = "PTR";

    pthread_mutex_lock( &shutdownMutex );
    DnsQ = QueueCreate(256);

    LogPrintNoArg( LOG_NOTICE, "Starting DNS thread" );

    while( !GlobalAbort && !threadAbort ) {
        qItem = (DNSItem_t *)QueueDequeueItem( DnsQ, 1000 );
        if( !qItem ) {
            continue;
        }

        command = qItem->command;
        dnsserver = NULL;
        type = "A";
        host = NULL;
        dnsType = NULL;

        /* Parse the command string */
        while( command ) {
            command = CommandLineParse( command, &line );
            if( !line ) {
                if( *command == '@' ) {
                    dnsserver = strdup(&command[1]);
                    free(command);
                } else if( !host ) {
                    /* Only the host */
                    dnsserver = NULL;
                    host = command;
                } else {
                    type = command;
                }
                command = NULL;
                continue;
            }

            if( *command == '@' ) {
                dnsserver = strdup(&command[1]);
                free( command );
            } else if ( !strcasecmp( command, "-x" ) ) {
                type = command;
            } else if ( !host ) {
                host = command;
            } else {
                type = command;
            }

            command = line;
            if( command && !*command ) {
                command = NULL;
            }
        }

        if( !strcasecmp( type, "-x" ) ) {
            type = type_ptr;
            response = in_addr_arpa( host );
            if( !response ) {
                response = (char *)malloc(strlen(host)+22);
                sprintf(response, "%s: Invalid Reverse", host);
                goto ShowResponse;
            }
            host = response;
        }

        if( type ) {
            item = BalancedBTreeFind( dnsTypeTree, (void *)&type, UNLOCKED );
            if( !item ) {
                /* WTF is this type?  Punt the request */
                response = (char *)malloc(strlen(host)+strlen(type)+22);
                sprintf(response, "%s %s: Unknown RR Type", host, type);
                goto ShowResponse;
            }
            dnsType = (DNSType_t *)item->item;
        }

        res_init();

        if( dnsserver ) {
            struct hostent  he_addr;
            struct hostent *result;
            int             lErrno;
            char            results[PACKETSZ];

            gethostbyname_r( dnsserver, &he_addr, results, 
                             PACKETSZ, &result, &lErrno );
            _res.nscount = 1;
            memcpy( &_res.nsaddr_list[0].sin_addr, he_addr.h_addr_list[0],
                    sizeof( _res.nsaddr_list[0].sin_addr ) );
            _res.nsaddr_list[0].sin_family = AF_INET;
            _res.nsaddr_list[0].sin_port = htons(53);
        }

        response = findRR( host, dnsType->type );
        if( !response ) {
            response = (char *)malloc(strlen(host)+strlen(dnsType->name)+15);
            sprintf(response, "%s %s: No results", host, dnsType->name);
        }

    ShowResponse:
        LogPrint( LOG_NOTICE, "%s", response );

        if( qItem->channel ) {
            LoggedChannelMessage( qItem->server, qItem->channel, response );
        } else {
            transmitMsg( qItem->server, TX_PRIVMSG, qItem->nick, response );
        }

        free( response );
        free( qItem );
    }

    if( QueueUsed( DnsQ ) != 0 ) {
        QueueClear( DnsQ, true );
    }

    QueueLock( DnsQ );
    QueueDestroy( DnsQ );
    LogPrintNoArg( LOG_NOTICE, "Shutting down DNS thread" );
    pthread_mutex_unlock( &shutdownMutex );
    return( NULL );
}

void botCmdDig( IRCServer_t *server, IRCChannel_t *channel, char *who, 
                char *msg, void *tag )
{
    DNSItem_t      *item;

    if( !msg ) {
        return;
    }

    item = (DNSItem_t *)malloc(sizeof(DNSItem_t));
    if( !item ) {
        return;
    }

    item->server = server;
    item->channel = channel;
    item->nick = who;
    item->command = msg;

    if( !GlobalAbort && !threadAbort ) {
        QueueEnqueueItem( DnsQ, item );
    }
}

char *botHelpDig( void *tag )
{
    static char *help = "Looks up hosts in DNS.  "
                        "Syntax: dig hostname type @server or "
                        "dig -x ipaddress @server";
    
    return( help );
}

/*
 * The following code is from contrib/query-loc-0.3.0 in BIND 9.3.2.  The 
 * README in that directory states it is under GPL for licensing, so rather 
 * than recreate it, I'm taking it, and reworking it for my uses.  The start
 * and end of said code is marked with "query-loc-0.3.0" in a comment
 */

/*
 * BEGIN code from query-loc-0.3.0
 */

/*
 ** IN_ADDR_ARPA -- Convert dotted quad string to reverse in-addr.arpa
 ** ------------------------------------------------------------------
 **
 **   Returns:
 **           Pointer to appropriate reverse in-addr.arpa name
 **           with trailing dot to force absolute domain name.
 **           NULL in case of invalid dotted quad input string.
 */

#ifndef ARPA_ROOT
#define ARPA_ROOT "in-addr.arpa"
#endif

char           *in_addr_arpa( char *dottedquad )
{
    static char     addrbuf[4 * 4 + sizeof(ARPA_ROOT) + 2];
    unsigned int    a[4];
    register int    n;

    n = sscanf(dottedquad, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]);
    switch (n) {
    case 4:
        sprintf(addrbuf, "%u.%u.%u.%u.%s.", a[3] & 0xff, a[2] & 0xff, 
                                            a[1] & 0xff, a[0] & 0xff,
                                            ARPA_ROOT);
        break;

    case 3:
        sprintf(addrbuf, "%u.%u.%u.%s.", a[2] & 0xff, a[1] & 0xff, a[0] & 0xff,
                                         ARPA_ROOT);
        break;

    case 2:
        sprintf(addrbuf, "%u.%u.%s.", a[1] & 0xff, a[0] & 0xff, ARPA_ROOT);
        break;

    case 1:
        sprintf(addrbuf, "%u.%s.", a[0] & 0xff, ARPA_ROOT);
        break;

    default:
        return (NULL);
    }

    while (--n >= 0) {
        if (a[n] > 255) {
            return (NULL);
        }
    }

    return (addrbuf);
}


/*
 * The code for these two functions is stolen from the examples in Liu and 
 * Albitz book "DNS and BIND" (O'Reilly). 
 */

/****************************************************************
 * skipName -- This routine skips over a domain name.  If the   *
 *     domain name expansion fails, it crashes.                 *
 *     dn_skipname() is probably not on your manual             *
 *     page; it is similar to dn_expand() except that it just   *
 *     skips over the name.  dn_skipname() is in res_comp.c if  *
 *     you need to find it.                                     *
 ****************************************************************/
int skipName(unsigned char *cp, unsigned char *endOfMsg)
{
    int             n;

    if ((n = dn_skipname(cp, endOfMsg)) < 0) {
        return( 0 );
    }
    return( n );
}

/****************************************************************
 * skipToData -- This routine advances the cp pointer to the    *
 *     start of the resource record data portion.  On the way,  *
 *     it fills in the type, class, ttl, and data length        *
 ****************************************************************/
int skipToData(unsigned char *cp, unsigned short *type, unsigned short *class, 
               unsigned int *ttl, unsigned short *dlen, 
               unsigned char *endOfMsg)
{
    u_char         *tmp_cp = cp;        /* temporary version of cp */

    /*
     * Skip the domain name; it matches the name we looked up 
     */
    tmp_cp += skipName(tmp_cp, endOfMsg);

    /*
     * Grab the type, class, and ttl.  GETSHORT and GETLONG
     * are macros defined in arpa/nameser.h.
     */
    GETSHORT(*type, tmp_cp);
    GETSHORT(*class, tmp_cp);
    GETLONG(*ttl, tmp_cp);
    GETSHORT(*dlen, tmp_cp);

    return (tmp_cp - cp);
}

/*
 * Returns a human-readable version of a DNS RR (resource record)
 * associated with the name 'domain'. If it does not find, ir returns NULL 
 * and sets rr_errno to explain why.
 * 
 * The code for this function is stolen from the examples in Liu and
 * Albitz book "DNS and BIND" (O'Reilly). 
 */
char           *findRR(char *domain, int requested_type)
{
    char           *result,
                   *message,
                   *temp;

    union {
        HEADER          hdr;    /* defined in resolv.h */
        u_char          buf[PACKETSZ];  /* defined in arpa/nameser.h */
    } response;                 /* response buffers */
    short           found = 0;
    int             responseLen;        /* buffer length */

    u_char         *cp;         /* character pointer to parse DNS packet */
    u_char         *endOfMsg;   /* need to know the end of the message */
    u_short         class;      /* classes defined in arpa/nameser.h */
    u_short         type;       /* types defined in arpa/nameser.h */
    u_int32_t       ttl;        /* resource record time to live */
    u_short         dlen;       /* size of resource record data */

    int             count;
    int             i;

    struct in_addr          addr;
    BalancedBTreeItem_t    *item;
    DNSType_t              *dnsType;
    unsigned char          *temp_cp;

    item = BalancedBTreeFind( dnsTypeNumTree, (void *)&requested_type, 
                              UNLOCKED );
    if( !item ) {
        /* WTF is this type?  Punt the request */
        return( NULL );
    }
    dnsType = (DNSType_t *)item->item;

    result = (char *) malloc(512);
    message = (char *) malloc(256);
    temp = (char *) malloc(MAXDNAME);

    sprintf( result, "%s %s: ", domain, dnsType->name );

    /*
     * Look up the records for the given domain name.
     * We expect the domain to be a fully qualified name, so
     * we use res_query().  If we wanted the resolver search 
     * algorithm, we would have used res_search() instead.
     */
    if ((responseLen = res_query(domain, C_IN, requested_type, 
                                 (u_char *)&response, sizeof(response))) <0) {
        free( result );
        free( message );
        free( temp );
        return NULL;
    }

    /*
     * Keep track of the end of the message so we don't 
     * pass it while parsing the response.  responseLen is 
     * the value returned by res_query.
     */
    endOfMsg = response.buf + responseLen;

    /*
     * Set a pointer to the start of the question section, 
     * which begins immediately AFTER the header.
     */
    cp = response.buf + sizeof(HEADER);

    /*
     * Skip over the whole question section.  The question 
     * section is comprised of a name, a type, and a class.  
     * QFIXEDSZ (defined in arpa/nameser.h) is the size of 
     * the type and class portions, which is fixed.  Therefore, 
     * we can skip the question section by skipping the 
     * name (at the beginning) and then advancing QFIXEDSZ.
     * After this calculation, cp points to the start of the 
     * answer section, which is a list of NS records.
     */
    cp += skipName(cp, endOfMsg) + QFIXEDSZ;

    count = ntohs(response.hdr.ancount) + ntohs(response.hdr.nscount);
    while ((--count >= 0) && (cp < endOfMsg)) {
        /*
         * Skip to the data portion of the resource record 
         */
        cp += skipToData(cp, &type, &class, &ttl, &dlen, endOfMsg);

        if (type == requested_type) {
            switch (requested_type) {
            case (T_NS):
            case (T_CNAME):
            case (T_PTR):
                if (dn_expand(response.buf, endOfMsg, cp, (char *)temp, 
                              MAXDNAME) <0) {
                    free( result );
                    free( message );
                    free( temp );
                    return NULL;
                }

                strcat(result, temp);
                strcat(result, " ");
                found = 1;
                break;
            case (T_A):
                bcopy((char *) cp, (char *) &addr, INADDRSZ);
                strcat(result, inet_ntoa(addr));
                strcat(result, " ");
                found = 1;
                break;
            case (T_MX):
                temp_cp = cp;
                GETSHORT(class, temp_cp);

                if (dn_expand(response.buf, endOfMsg, temp_cp, (char *)temp, 
                              MAXDNAME) <0) {
                    free( result );
                    free( message );
                    free( temp );
                    return NULL;
                }

                sprintf(message, "(%d) %s ", class, temp);
                strcat(result, message);
                found = 1;
                break;
            case (T_TXT):
                strncat(result, (char *)cp, dlen);
                strcat(result, " ");
                found = 1;
                break;
            case (T_SOA):
                temp_cp = cp;

                if ((count = dn_expand(response.buf, endOfMsg, temp_cp,
                                       (char *)temp, MAXDNAME)) <0) {
                    free( result );
                    free( message );
                    free( temp );
                    return NULL;
                }

                strcat(result, temp);
                strcat(result, ". ");

                temp_cp += count;

                if ((count = dn_expand(response.buf, endOfMsg, temp_cp,
                                       (char *)temp, MAXDNAME)) <0) {
                    free( result );
                    free( message );
                    free( temp );
                    return NULL;
                }

                strcat(result, temp);
                strcat(result, ". ");

                temp_cp += count;

                for( i=0; i<5; i++) {
                    GETLONG(ttl, temp_cp);
                    sprintf(message, "%d ", ttl);
                    strcat(result, message);
                }

                found = 1;
                break;
            default:
                LogPrint(LOG_NOTICE, "Unexpected type %u", requested_type);
                free( result );
                free( message );
                free( temp );
                return NULL;
            }
        }

        /*
         * Advance the pointer over the resource record data 
         */
        cp += dlen;

    }

    free( message );
    free( temp );

    if (found) {
        return( result );
    } else {
        free( result );
        return( NULL );
    }
}


/*
 * END code from query-loc-0.3.0
 */

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
