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

#ifndef db_schema_h_
#define db_schema_h_

#include "environment.h"

static char db_schema_h_ident[] _UNUSED_ = 
    "$Id$";

#define CURRENT_SCHEMA  4


char *defSchema[] = {
"CREATE TABLE `channels` (\n"
"  `chanid` int(11) NOT NULL auto_increment,\n"
"  `serverid` int(11) NOT NULL default '0',\n"
"  `channel` varchar(64) NOT NULL default '',\n"
"  `notify` int(11) NOT NULL default '0',\n"
"  `url` text NOT NULL,\n"
"  `notifywindow` int(11) NOT NULL default '24',\n"
"  `cmdChar` char(1) NOT NULL default '',\n"
"  PRIMARY KEY  (`chanid`),\n"
"  KEY `serverChan` (`serverid`,`channel`)\n"
") TYPE=MyISAM PACK_KEYS=1;\n",

"CREATE TABLE `irclog` (\n"
"  `msgid` int(11) NOT NULL auto_increment,\n"
"  `chanid` int(11) NOT NULL default '0',\n"
"  `timestamp` int(14) NOT NULL default '0',\n"
"  `nick` varchar(64) NOT NULL default '',\n"
"  `msgtype` int(11) NOT NULL default '0',\n"
"  `message` text NOT NULL,\n"
"  PRIMARY KEY  (`msgid`),\n"
"  KEY `timeChan` (`timestamp`,`chanid`),\n"
"  KEY `messageType` (`chanid`,`msgtype`,`timestamp`),\n"
"  FULLTEXT KEY `searchtext` (`nick`,`message`)\n"
") TYPE=MyISAM;\n",

"CREATE TABLE `nicks` (\n"
"  `chanid` int(11) NOT NULL default '0',\n"
"  `nick` varchar(64) NOT NULL default '',\n"
"  `lastseen` int(11) NOT NULL default '0',\n"
"  `lastnotice` int(11) NOT NULL default '0',\n"
"  `present` int(11) NOT NULL default '0',\n"
"  PRIMARY KEY  (`chanid`,`nick`)\n"
") TYPE=MyISAM;\n",

"CREATE TABLE `plugins` (\n"
"  `pluginName` varchar(64) NOT NULL default '',\n"
"  `libName` varchar(64) NOT NULL default '',\n"
"  `preload` int(11) NOT NULL default '0',\n"
"  `arguments` varchar(255) NOT NULL default '',\n"
"  PRIMARY KEY  (`pluginName`)\n"
") TYPE=MyISAM;\n",

"CREATE TABLE `servers` (\n"
"  `serverid` int(11) NOT NULL auto_increment,\n"
"  `server` varchar(255) NOT NULL default '',\n"
"  `port` int(11) NOT NULL default '0',\n"
"  `nick` varchar(64) NOT NULL default '',\n"
"  `username` varchar(16) NOT NULL default '',\n"
"  `realname` varchar(255) NOT NULL default '',\n"
"  `nickserv` varchar(64) NOT NULL default '',\n"
"  `nickservmsg` varchar(255) NOT NULL default '',\n"
"  PRIMARY KEY  (`serverid`),\n"
"  KEY `serverNick` (`server`,`port`,`nick`)\n"
") TYPE=MyISAM PACK_KEYS=0;\n",

"CREATE TABLE `settings` (\n"
"  `name` varchar(80) NOT NULL default '',\n"
"  `value` varchar(255) NOT NULL default '',\n"
"  PRIMARY KEY  (`name`)\n"
") TYPE=MyISAM;\n",

"CREATE TABLE `nickhistory` (\n"
"  `chanid` int(11) NOT NULL default '0',\n"
"  `nick` varchar(64) NOT NULL default '',\n"
"  `histType` int(11) NOT NULL default '0',\n"
"  `timestamp` int(11) NOT NULL default '0',\n"
"  PRIMARY KEY  (`chanid`,`histType`,`timestamp`,`nick`)\n"
") TYPE=MyISAM;\n"
};
int defSchemaCount = NELEMENTS(defSchema);

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
