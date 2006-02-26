<?php
/**
 * IRC Channels
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
 * @uses        includes/irc_servers.php
 * @uses        $Servers
 *
/**/

/**
 * @global  array   $GLOBALS['Channels']
 * @name    $Channels
/**/
    $Channels = array();

// Load all of the servers
    $sh = $db->query('SELECT channels.*,
                             MAX(irclog.timestamp) AS latest_entry
                        FROM channels
                             NATURAL LEFT JOIN irclog
                    GROUP BY chanid');
    while ($row = $sh->fetch_assoc()) {
        $Channels[$row['chanid']] = new irc_channel($row);
    }
    $sh->finish();


/**
 * Class to hold the IRC channels that are being logged.
/**/
class irc_channel {

    var $chanid;
    var $serverid;
    var $channel;
    var $url;
    var $notifywindow;
    var $cmdChar;
    var $latest_entry;

    var $server;

/**
 * Object constructor
 *
 * @param array $channel_vars   Hash of channel vars from the database.
/**/
    function __construct($channel_vars) {
        global $Servers;
    // Assign the various channel vars
        $this->chanid       = $channel_vars['chanid'];
        $this->serverid     = $channel_vars['serverid'];
        $this->channel      = $channel_vars['channel'];
        $this->url          = $channel_vars['url'];
        $this->notifywindow = $channel_vars['notifywindow'];
        $this->cmdChar      = $channel_vars['cmdChar'];
        $this->latest_entry = $channel_vars['latest_entry'];
    // Keep a reference to this channel's server
        $this->server       =& $Servers[$this->serverid];
    // Add this channel to its parent server
        $Servers[$this->serverid]->add_channel($this);
    }

/**
 * Placeholder constructor for php4 compatibility
 *
 * @param array $channel_vars   Hash of channel vars from the database.
/**/
    function &irc_channel($channel_vars) {
        return $this->__construct($channel_vars);
    }

}
