-- phpMyAdmin SQL Dump
-- version 2.6.4-pl1-Debian-1ubuntu1
-- http://www.phpmyadmin.net
-- 
-- Host: localhost
-- Generation Time: Jun 23, 2006 at 05:12 PM
-- Server version: 4.0.24
-- PHP Version: 4.4.0-3
-- 
-- Database: `beirdobot`
-- 

-- --------------------------------------------------------

-- 
-- Table structure for table `channels`
-- 

CREATE TABLE `channels` (
  `chanid` int(11) NOT NULL auto_increment,
  `serverid` int(11) NOT NULL default '0',
  `channel` varchar(64) NOT NULL default '',
  `notify` int(11) NOT NULL default '0',
  `url` text NOT NULL,
  `notifywindow` int(11) NOT NULL default '24',
  `cmdChar` char(1) NOT NULL default '',
  PRIMARY KEY  (`chanid`),
  KEY `serverChan` (`serverid`,`channel`)
) TYPE=MyISAM PACK_KEYS=1;

-- --------------------------------------------------------

-- 
-- Table structure for table `irclog`
-- 

CREATE TABLE `irclog` (
  `msgid` int(11) NOT NULL auto_increment,
  `chanid` int(11) NOT NULL default '0',
  `timestamp` int(14) NOT NULL default '0',
  `nick` varchar(64) NOT NULL default '',
  `msgtype` int(11) NOT NULL default '0',
  `message` text NOT NULL,
  PRIMARY KEY  (`msgid`),
  KEY `timeChan` (`chanid`,`timestamp`),
  KEY `messageType` (`msgtype`,`chanid`,`timestamp`),
  KEY `chanid` (`chanid`),
  KEY `timestamp` (`timestamp`),
  FULLTEXT KEY `searchtext` (`nick`,`message`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `nickhistory`
-- 

CREATE TABLE `nickhistory` (
  `chanid` int(11) NOT NULL default '0',
  `nick` varchar(64) NOT NULL default '',
  `histType` int(11) NOT NULL default '0',
  `timestamp` int(11) NOT NULL default '0',
  PRIMARY KEY  (`chanid`,`histType`,`timestamp`,`nick`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `nicks`
-- 

CREATE TABLE `nicks` (
  `chanid` int(11) NOT NULL default '0',
  `nick` varchar(64) NOT NULL default '',
  `lastseen` int(11) NOT NULL default '0',
  `lastnotice` int(11) NOT NULL default '0',
  `present` int(11) NOT NULL default '0',
  PRIMARY KEY  (`chanid`,`nick`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `plugin_rssfeed`
-- 

CREATE TABLE `plugin_rssfeed` (
  `feedid` int(11) NOT NULL auto_increment,
  `chanid` int(11) NOT NULL default '0',
  `url` varchar(255) NOT NULL default '',
  `prefix` varchar(64) NOT NULL default '',
  `timeout` int(11) NOT NULL default '0',
  `lastpost` int(11) NOT NULL default '0',
  `feedoffset` int(11) NOT NULL default '0',
  PRIMARY KEY  (`feedid`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `plugin_trac`
-- 

CREATE TABLE `plugin_trac` (
  `chanid` int(11) NOT NULL default '0',
  `url` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`chanid`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `plugins`
-- 

CREATE TABLE `plugins` (
  `pluginName` varchar(64) NOT NULL default '',
  `libName` varchar(64) NOT NULL default '',
  `preload` int(11) NOT NULL default '0',
  `arguments` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`pluginName`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `servers`
-- 

CREATE TABLE `servers` (
  `serverid` int(11) NOT NULL auto_increment,
  `server` varchar(255) NOT NULL default '',
  `port` int(11) NOT NULL default '0',
  `password` varchar(255) NOT NULL default '',
  `nick` varchar(64) NOT NULL default '',
  `username` varchar(16) NOT NULL default '',
  `realname` varchar(255) NOT NULL default '',
  `nickserv` varchar(64) NOT NULL default '',
  `nickservmsg` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`serverid`),
  KEY `serverNick` (`server`,`port`,`nick`)
) TYPE=MyISAM PACK_KEYS=0;

-- --------------------------------------------------------

-- 
-- Table structure for table `settings`
-- 

CREATE TABLE `settings` (
  `name` varchar(80) NOT NULL default '',
  `value` varchar(255) NOT NULL default '',
  PRIMARY KEY  (`name`)
) TYPE=MyISAM;

-- --------------------------------------------------------

-- 
-- Table structure for table `userauth`
-- 

CREATE TABLE `userauth` (
  `username` varchar(64) NOT NULL default '',
  `digest` enum('md4','md5','sha1') NOT NULL default 'md4',
  `seed` varchar(20) NOT NULL default '',
  `key` varchar(16) NOT NULL default '',
  `keyIndex` int(11) NOT NULL default '0',
  PRIMARY KEY  (`username`)
) TYPE=MyISAM;
