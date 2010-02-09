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

#include "environment.h"
#include "clucene.h"
#include <CLucene.h>
#include <CLucene/config/repl_tchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#ifndef WEBSERVICE
#include "protos.h"
#include "logging.h"
#include "queue.h"
#endif

using namespace std;
using namespace lucene::index;
using namespace lucene::analysis;
using namespace lucene::util;
using namespace lucene::store;
using namespace lucene::queryParser;
using namespace lucene::document;
using namespace lucene::search;

static char ident[] _UNUSED_= 
    "$Id$";

#define MAX_STRING_LEN 1024

#ifndef WEBSERVICE
typedef struct {
    unsigned long   id;
    int             chanid;
    char           *nick;
    char           *text;
    unsigned long   timestamp;
} IndexItem_t;

pthread_t       cluceneThreadId;
pthread_t       kickerThreadId;
int             docmax = 0;

void addLogentry( Document *doc, unsigned long *tb, IndexItem_t *item );
int loadLogentry( Document *doc, unsigned long tb );
void *clucene_thread( void *arg );
void *kicker_thread( void *arg );
IndexWriter *getWriter( int clear );
void closeWriter( IndexWriter *writer );
#endif
char *clucene_escape( char *text );

/* The C interface portion */
extern "C" {
#ifndef WEBSERVICE
    QueueObject_t  *IndexQ;

    void clucene_init(int clear)
    {
        int        *clr;

        clr = (int *)malloc(sizeof(int));
        *clr = clear;

        IndexQ = QueueCreate( 1024 );

        versionAdd( (char *)"CLucene", (char *)_CL_VERSION );

        /* Start the threads */
        thread_create( &cluceneThreadId, clucene_thread, clr, 
                       (char *)"thread_clucene", NULL );
        if( !clear ) {
            thread_create( &kickerThreadId, kicker_thread, NULL, 
                           (char *)"thread_kicker", NULL );
        }
    }
#endif

    void clucene_shutdown(void)
    {
        _lucene_shutdown();
    }

#ifndef WEBSERVICE
    void clucene_add( int chanid, char *nick, char *text, 
                      unsigned long timestamp )
    {
        IndexItem_t        *item;

        item = (IndexItem_t *)malloc(sizeof(IndexItem_t));
        memset( item, 0, sizeof(IndexItem_t) );

        item->chanid    = chanid;
        item->nick      = nick ? strdup(nick) : NULL;
        item->text      = text ? strdup(text) : NULL;
        item->timestamp = (timestamp / SEARCH_WINDOW);

        QueueEnqueueItem( IndexQ, item );
    }
#endif

    SearchResults_t *clucene_search( int chanid, char *text, int *count, 
                                     int max )
    {
        IndexReader            *reader;
        WhitespaceAnalyzer      an;
        IndexSearcher          *s;
        Query                  *q;
        Hits                   *h;
        Document               *d;
        static TCHAR            query[MAX_STRING_LEN];
        char                   *esctext;
        SearchResults_t        *results;
        int                     i;
        char                   *ts;

        reader = IndexReader::open(CLUCENE_INDEX_DIR);
        s = _CLNEW IndexSearcher(reader);

        esctext = clucene_escape(text);
        _sntprintf( query, MAX_STRING_LEN, 
                    _T("chanid:%ld AND (text:\"%s\" OR nick:\"%s\")"), chanid, 
                    esctext, esctext );
#ifndef WEBSERVICE
        LogPrint(LOG_INFO, "Query: %ls", query);
#endif
        q = QueryParser::parse(query, _T("text"), &an);
        h = s->search(q);
        if( h->length() == 0 ) {
            *count = 0;
            reader->close();
            _CLDELETE(h);
            _CLDELETE(q);
            _CLDELETE(s);
            _CLDELETE(reader);
            return( NULL );
        }
        *count = (h->length() > max ? max : h->length());
        results = (SearchResults_t *)malloc(*count * sizeof(SearchResults_t));
        for( i = 0; i < *count; i++ ) {
            d = &h->doc(i);
            ts = STRDUP_TtoA(d->get(_T("timestamp")));
            results[i].timestamp = atoi(ts) * SEARCH_WINDOW;
            results[i].score = h->score(i);
            free( ts );
        }

        reader->close();
        _CLDELETE(h);
        _CLDELETE(q);
        _CLDELETE(s);
        _CLDELETE(reader);

        return( results );
    }
}

/* C++ internals */

char *clucene_escape( char *text )
{
    static char         buf[MAX_STRING_LEN];
    static const char   chars[] = "+-&|!(){}[]^\"~*?:\\";
    int                 len;
    int                 i;
    int                 j;

    len = strlen(text);
    for( i = 0, j = 0; i < len && j < MAX_STRING_LEN - 1; i++ ) {
        if( strchr( chars, (int)text[i] ) ) {
            buf[j++] = '\\';
        }
        buf[j++] = text[i];
    }
    buf[j] = '\0';
    return( buf );
}

#ifndef WEBSERVICE
void addLogentry( Document *doc, unsigned long *tb, IndexItem_t *item )
{
    static TCHAR            buf[MAX_STRING_LEN];
    IndexWriter            *writer;

    if( *tb != item->timestamp ) {
        if( *tb != 0 ) {
            writer = getWriter(false);
            writer->addDocument( doc );
            closeWriter( writer );
            doc->clear();
        }

        if( item->text == NULL ) {
            /* Keepalive */
            *tb = 0;
            return;
        }

        if( !loadLogentry( doc, item->timestamp ) ) {
            _sntprintf( buf, MAX_STRING_LEN, _T("%ld"), item->timestamp );
            doc->add( *_CLNEW Field( _T("timestamp"), buf, 
                      Field::STORE_YES | Field::INDEX_TOKENIZED ) );

            _sntprintf( buf, MAX_STRING_LEN, _T("%ld"), item->chanid );
            doc->add( *_CLNEW Field( _T("chanid"), buf, 
                    Field::STORE_YES | Field::INDEX_TOKENIZED ) );
        }
        *tb = item->timestamp;
    }

    STRCPY_AtoT(buf, item->nick, MAX_STRING_LEN);
    doc->add( *_CLNEW Field( _T("nick"), buf, 
              Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    STRCPY_AtoT(buf, item->text, MAX_STRING_LEN);
    doc->add( *_CLNEW Field( _T("text"), buf, 
              Field::STORE_YES | Field::INDEX_TOKENIZED ) );
}

int loadLogentry( Document *doc, unsigned long tb )
{
    IndexReader                *reader;
    WhitespaceAnalyzer          an;
    IndexSearcher              *s;
    Query                      *q;
    Hits                       *h;
    Document                   *d;
    DocumentFieldEnumeration   *fields;
    Field                      *field;
    static TCHAR                query[80];
    int                         len;

    reader = IndexReader::open(CLUCENE_INDEX_DIR);
    s = _CLNEW IndexSearcher(reader);

    _sntprintf( query, 80, _T("%ld"), tb );
    q = QueryParser::parse(query, _T("timestamp"), &an);
    h = s->search(q);
    len = h->length();
#if 0
    LogPrint( LOG_INFO, "Q: %ls  len: %d", q->toString(), len );
#endif
    if( h->length() == 0 ) {
        reader->close();
        _CLDELETE(h);
        _CLDELETE(q);
        _CLDELETE(s);
        _CLDELETE(reader);
        return( 0 );
    }
    d = &h->doc(0);
    fields = d->fields();
    doc->clear();
    /* Recreate the current document */
    while( (field = fields->nextElement()) ) {
        doc->add( *_CLNEW Field( field->name(), field->stringValue(), 
                                 Field::STORE_YES | Field::INDEX_TOKENIZED ) );
    }
    LogPrint( LOG_INFO, "Deleting document %lld", h->id(0) );
    reader->deleteDocument( h->id(0) );
    reader->close();

    _CLDELETE(h);
    _CLDELETE(q);
    _CLDELETE(s);
    _CLDELETE(reader);

    return( 1 );
}

IndexWriter *getWriter( int clear ) 
{
    WhitespaceAnalyzer *an;
    IndexWriter        *writer = NULL;

#if 0
    LogPrintNoArg(LOG_INFO, "Opening writer");
#endif
    an = _CLNEW WhitespaceAnalyzer;
    if ( !clear && IndexReader::indexExists(CLUCENE_INDEX_DIR) ){
        if ( IndexReader::isLocked(CLUCENE_INDEX_DIR) ){
            LogPrintNoArg( LOG_INFO, "Index was locked... unlocking it.");
            IndexReader::unlock(CLUCENE_INDEX_DIR);
        }

        writer = _CLNEW IndexWriter( CLUCENE_INDEX_DIR, an, false);
    } else {
        writer = _CLNEW IndexWriter( CLUCENE_INDEX_DIR, an, true);
    }

    writer->setMaxFieldLength(1000000);

    return( writer );
}

void closeWriter( IndexWriter *writer )
{
    WhitespaceAnalyzer *an;

#if 0
    LogPrintNoArg( LOG_INFO, "Closing Writer" );
#endif
    an = (WhitespaceAnalyzer *)writer->getAnalyzer();
    writer->close();
    _CLDELETE(an);
    _CLDELETE(writer);
}

void *kicker_thread( void *arg )
{
    struct timeval  tv;
    unsigned long   now;
    unsigned long   target;
    int             i;

    LogPrintNoArg( LOG_INFO, "Starting CLucene Kicker thread" );
    while( !GlobalAbort ) {
        gettimeofday( &tv, NULL );
        now = tv.tv_sec;
        target = ((now / SEARCH_WINDOW) + 1) * SEARCH_WINDOW;
        sleep( target - now );

        LogPrint( LOG_INFO, "Kicking %d channel indexes", docmax );
        for( i = 0; i < docmax; i++ ) {
            clucene_add( i + 1, NULL, NULL, target / SEARCH_WINDOW );
        }
    }
    LogPrintNoArg( LOG_INFO, "Ending CLucene Kicker thread" );

    return( NULL );
}

void *clucene_thread( void *arg ) 
{
    IndexItem_t        *item;
    IndexWriter        *writer = NULL;
    Document          **doc = NULL;
    unsigned long      *lasttb = NULL;
    int                 i;
    int                 clear;

    clear = *(int *)arg;
    LogPrint(LOG_INFO, "Clear: %d", clear );

    LogPrintNoArg( LOG_NOTICE, "Starting CLucene thread" );
    LogPrint( LOG_INFO, "Using CLucene v%s index in %s", _CL_VERSION,
              CLUCENE_INDEX_DIR );

    writer = getWriter(clear);
    LogPrint( LOG_INFO, "%lld documents indexed", writer->docCount() );
    LogPrintNoArg( LOG_INFO, "Optimizing index" );
    writer->optimize();
    LogPrintNoArg( LOG_INFO, "Finished optimizing index" );
    closeWriter( writer );

    while( !GlobalAbort ) {
        item = (IndexItem_t *)QueueDequeueItem( IndexQ, 1000 );
        if( !item ) {
            continue;
        }

        if( item->chanid > docmax ) {
            doc = (Document **)realloc(doc, item->chanid * sizeof(Document *));
            lasttb = (unsigned long *)realloc(lasttb, 
                                        item->chanid * sizeof(unsigned long ));
            for( i = docmax; i < item->chanid; i++ ) {
                doc[i] = new Document;
                doc[i]->clear();
                lasttb[i] = 0;
            }
            docmax = item->chanid;
        }

        addLogentry( doc[item->chanid - 1], &lasttb[item->chanid - 1], item );

        if( item->nick ) {
            free( item->nick );
        }

        if( item->text ) {
            free( item->text );
        }
        free( item );
    }

    LogPrintNoArg( LOG_NOTICE, "Ending CLucene thread" );

    writer = getWriter(0);
    for( i = 0; i < docmax; i++ ) {
        if( lasttb[i] != 0 ) {
            writer->addDocument( doc[i] );
            _CLDELETE( doc[i] );
        }
    }
    closeWriter( writer );

    free( lasttb );
    free( doc );

    return(NULL);
}
#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
