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
#include "protos.h"
#include "logging.h"
#include "queue.h"

using namespace std;
using namespace lucene::index;
using namespace lucene::analysis;
using namespace lucene::util;
using namespace lucene::store;
using namespace lucene::document;

static char ident[] _UNUSED_= 
    "$Id$";

#define CLUCENE_INDEX_FILE CLUCENE_INDEX_DIR "/indexFile"

typedef struct {
    unsigned long   id;
    int             chanid;
    char           *nick;
    int             msgType;
    char           *text;
    unsigned long   timestamp;
} IndexItem_t;

QueueObject_t  *IndexQ;
pthread_t       cluceneThreadId;

void addLogentry( IndexWriter *writer, IndexItem_t *item );
void *clucene_thread( void *arg );

/* The C interface portion */
extern "C" {

    void clucene_init(void)
    {
        IndexQ = QueueCreate( 1024 );

        versionAdd( (char *)"CLucene", (char *)_CL_VERSION );

        /* Start the thread */
        thread_create( &cluceneThreadId, clucene_thread, NULL, 
                       (char *)"thread_clucene", NULL );
    }

    void clucene_shutdown(void)
    {
        _lucene_shutdown();
    }

    void clucene_add( unsigned long id, int chanid, char *nick, 
                      int msgType, char *text, unsigned long timestamp )
    {
        IndexItem_t        *item;

        item = (IndexItem_t *)malloc(sizeof(IndexItem_t));
        memset( item, 0, sizeof(IndexItem_t) );

        item->id        = id;
        item->chanid    = chanid;
        item->nick      = strdup(nick);
        item->msgType   = msgType;
        item->text      = strdup(text);
        item->timestamp = timestamp;

        QueueEnqueueItem( IndexQ, item );
    }
}

/* C++ internals */

#define MAX_STRING_LEN 1024

void addLogentry( IndexWriter *writer, IndexItem_t *item )
{
    static Document     doc;
    static TCHAR        buf[MAX_STRING_LEN];

    doc.clear();

    _sntprintf( buf, MAX_STRING_LEN, _T("%ld"), item->id );
    doc.add( *_CLNEW Field( _T("id"), buf, 
             Field::STORE_YES | Field::INDEX_UNTOKENIZED ) );

    _sntprintf( buf, MAX_STRING_LEN, _T("%ld"), item->chanid );
    doc.add( *_CLNEW Field( _T("chanid"), buf, 
             Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    STRCPY_AtoT(buf, item->nick, MAX_STRING_LEN);
    doc.add( *_CLNEW Field( _T("nick"), buf, 
             Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    _sntprintf( buf, MAX_STRING_LEN, _T("%ld"), item->msgType );
    doc.add( *_CLNEW Field( _T("msgtype"), buf, 
             Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    STRCPY_AtoT(buf, item->text, MAX_STRING_LEN);
    doc.add( *_CLNEW Field( _T("text"), buf, 
             Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    DateField::timeToString(item->timestamp, buf);
    doc.add( *_CLNEW Field( _T("timestamp"), buf, 
             Field::STORE_YES | Field::INDEX_TOKENIZED ) );

    writer->addDocument( &doc );
}


void *clucene_thread( void *arg ) 
{
    IndexItem_t        *item;
    IndexWriter        *writer = NULL;
    lucene::analysis::WhitespaceAnalyzer an;

    LogPrintNoArg( LOG_NOTICE, "Starting CLucene thread" );
    LogPrint( LOG_INFO, "Using CLucene v%s index in %s", _CL_VERSION,
              CLUCENE_INDEX_DIR );

    if ( IndexReader::indexExists(CLUCENE_INDEX_FILE) ){
        if ( IndexReader::isLocked(CLUCENE_INDEX_FILE) ){
            LogPrintNoArg( LOG_INFO, "Index was locked... unlocking it.");
            IndexReader::unlock(CLUCENE_INDEX_FILE);
        }

        writer = _CLNEW IndexWriter( CLUCENE_INDEX_FILE, &an, false);
    } else {
        writer = _CLNEW IndexWriter( CLUCENE_INDEX_FILE ,&an, true);
    }

    writer->setMaxFieldLength(MAX_STRING_LEN);

    LogPrintNoArg( LOG_INFO, "Optimizing index" );
    writer->optimize();
    LogPrintNoArg( LOG_INFO, "Finished optimizing index" );

    while( !BotDone ) {
        item = (IndexItem_t *)QueueDequeueItem( IndexQ, -1 );
        if( !item ) {
            continue;
        }

        addLogentry( writer, item );

        free( item->nick );
        free( item->text );
        free( item );
    }

    LogPrintNoArg( LOG_NOTICE, "Ending CLucene thread" );
    writer->close();
    _CLDELETE(writer);
    return(NULL);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
