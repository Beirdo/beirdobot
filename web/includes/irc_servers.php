<?php
/**
 * IRC Servers
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
/**/

/**
 * @global  array   $GLOBALS['Servers']
 * @name    $Servers
/**/
    $Servers = array();

// Load all of the servers
    $sh = $db->query('SELECT * FROM servers');
    while ($row = $sh->fetch_assoc()) {
        $Servers[$row['serverid']] = new irc_server($row);
    }
    $sh->finish();

/**
 * Class to hold the IRC servers that are being logged.
/**/
class irc_server {

    var $serverid;
    var $server;
    var $port;
    var $nick;
    var $username;
    var $realname;
    var $nickserv;
    var $nickservmsg;

    var $channels = array();

/**
 * Object constructor
 *
 * @param array $server_vars   Hash of server vars from the database.
/**/
    function __construct($server_vars) {
        $this->serverid    = $server_vars['serverid'];
        $this->server      = $server_vars['server'];
        $this->port        = $server_vars['port'];
        $this->nick        = $server_vars['nick'];
        $this->username    = $server_vars['username'];
        $this->realname    = $server_vars['realname'];
        $this->nickserv    = $server_vars['nickserv'];
        $this->nickservmsg = $server_vars['nickservmsg'];
    }

/**
 * Placeholder constructor for php4 compatibility
 *
 * @param array $server_vars   Hash of server vars from the database.
/**/
    function &irc_server($server_vars) {
        return $this->__construct($server_vars);
    }

/**
 * Add an irc_channel object to this server's channel list.
 *
 * @param irc_channel $channel  The channel object to add to the list.
/**/
    function add_channel(&$channel) {
        $this->channels[$channel->chanid] =& $channel;
    }
}

