<?php
/**
 * Print information about a specific irc channel
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
 * @global  mixed   $GLOBALS['Channel']
 * @name    $Channel
/**/
    $Channel = $Channels[$Path[1]];

// Unknown channel
    if (empty($Channel)) {
        $Error = 'Unknown channel:  '.$Path[1];
        require 'templates/_error.php';
        exit;
    }

// Load the last N minutes of messages from this channel
    $Channel->load_messages(time() - (60 * 15));

// Load the names of the users currently logged into the channel
    $Channel->load_users();

// Print the page
    include 'templates/channel.php';
