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
 * @uses        includes/irc_messages.php
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
                             MAX(irclog.timestamp) AS last_entry,
                             MIN(irclog.timestamp) AS first_entry
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
    var $last_entry;
    var $first_entry;

    var $server;
    var $messages = array();
    var $users    = array();

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
        $this->last_entry   = $channel_vars['last_entry'];
        $this->first_entry  = $channel_vars['first_entry'];
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

/**
 * Load all of the messages from the requested channel that were sent between
 * $from and $to.
 *
 * @param int $from timestamp of the first message to load
 * @param int $to   timestamp of the last message to load (default: now)
/**/
    function load_messages($from, $to = NULL) {
        global $db;
    // Default the start time to this morning
        if (is_null($from))
            $from = mktime(0, 0, 0);
    // Default the end time to now
        if (is_null($to))
            $to = time();
    // Load the messages
        $sh = $db->query('SELECT *
                            FROM irclog
                           WHERE chanid=?
                                 AND timestamp >= ?
                                 AND timestamp <= ?
                        ORDER BY timestamp ASC',
                         $this->chanid,
                         $from,
                         $to
                        );
        while ($row = $sh->fetch_assoc()) {
            $this->messages[$row['msgid']] = new irc_message($row);
        }
        $sh->finish();
    }

/**
 * Load all nicks currently in this channel, or those at time $time
 *
 * @param int $time The requested timestamp to pull the user list from.
/**/
    function load_users($time=NULL) {
        global $db;
    // Default the end time to now
        if (is_null($time) || $time == time()) {
        // Load the messages
            $sh = $db->query('SELECT *
                                FROM nicks
                               WHERE chanid=?
                                     AND present = 1
                            ORDER BY nick',
                             $this->chanid
                            );
            while ($row = $sh->fetch_assoc()) {
                $this->users[$row['nick']] = $row;
            }
            $sh->finish();
        }
    // If not now, we need to do some heavier math
        else {
        // First, we find the most recent bot login before the requested time period
            $start = $db->query_col('SELECT MAX(timestamp)
                                       FROM nickhistory
                                      WHERE chanid = ?
                                            AND timestamp <= ?
                                            AND histType   = 0',
                                    $this->chanid,
                                    $time);
            if (empty($start))
                $start = 0;
        // Then we query everything
            $sh = $db->query('SELECT *,
                                     IF(SUM(IF(histType<=2, 1, -1)) > 0, 1, 0) AS present
                                FROM nickhistory
                               WHERE chanid = ?
                                     AND timestamp >= ?
                                     AND timestamp <= ?
                            GROUP BY nick',
                             $this->chanid,
                             $start,
                             $time
                            );
            while ($row = $sh->fetch_assoc()) {
                $this->users[$row['nick']] = $row;
            }
            $sh->finish();
        }
    }

/**
 * Prints the channel log for the requested time period.
 *
 * @param int    $start  The start time
 * @param int    $end    The end time
 * @param string $format Format to print out (currently only one option:  table)
 * @param bool   $cache  Cache the results (automatically disabled without both start and end times)
/**/
    function print_log($start, $end, $format='table', $cache=true) {
    // Unknown format
        if ($format != 'table')
            return "Unknown format:  $format";
    // Don't cache without both start and end times (in the past)
        if (empty($start) || empty($end) || $end >= time())
            $cache = false;
    // Default the start time to this morning
        if (empty($start))
            $start = mktime(0, 0, 0);
    // Name of the cache file
        $cachefile = "data/static/$this->chanid.$start.$end.$format";
    // Return the cached version?
        if (file_exists($cachefile) && filesize($cachefile) > 0) {
            include $cachefile;
            return;
        }
    // No cached version?  Generate.
        global $db;
    // Start the output buffer
        ob_start();
    // Load the header
        require "templates/log/{$format}_head.php";
    // Cache?
        if ($cache) {
            $fp = fopen($cachefile, 'w');
            if ($fp === false) {
                # error, error!
            }
        }
    // Load the messages.  Default end is time(), but don't actually set the
    // variable so we can provide
        $sh = $db->query('SELECT *
                            FROM irclog
                           WHERE chanid=?
                                 AND timestamp >= ?
                                 AND timestamp <= ?
                        ORDER BY timestamp ASC',
                         $this->chanid,
                         $start,
                         ($end ? $end : time())
                        );
    // Empty result set?
        if ($sh->num_rows() < 1) {
            require "templates/log/{$format}_empty.php";
        }
    // Print the messages in a memory-friendly fashion
        else {
            $lines    = 0;
            $last_day = null;
            while ($row = $sh->fetch_assoc()) {
                $lines++;
            // Load the message into an object
                $message = new irc_message($row);
            // Keep track of the current day, for separators
                $this_day = date('Y-m-d', $message->timestamp);
            // Print out a nice separator between each day?
                if ($last_day != $this_day) {
                    @include "templates/log/{$format}_newday.php";
                    $last_day = $this_day;
                }
            // Print the message content
                require "templates/log/{$format}_message.php";
            // Print/flush the buffer every 250 lines so we don't use up too much memory.
                if ($lines % 250 == 0) {
                // Extract what we have so far from the buffer
                    $log = ob_get_contents();
                    ob_end_clean();
                // Print what we have so far
                    echo $log;
                // Cache?
                    if ($fp)
                        fwrite($fp, $log);
                // Start the output buffer again
                    ob_start();
                }
            }
        }
        $sh->finish();
    // Load the footer
        require "templates/log/{$format}_foot.php";
    // Get the last part of the buffer
        $log = ob_get_contents();
        ob_end_clean();
    // Print the string
        echo $log;
    // Cache?
        if ($fp) {
            fwrite($fp, $log);
            fclose($fp);
        }
    }

}

