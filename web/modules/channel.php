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
    }

// Channel history page?
    if ($Path[2] == 'history') {
        require_once 'modules/channel/history.php';
        exit;
    }

// Requesting a specific time period?
    $_GET['start'] = intVal($_GET['start']);
    $_GET['end']   = intVal($_GET['end']);
    if (empty($_GET['start']))
        $_GET['start'] = time() - (60 * 15);
    if (empty($_GET['end']))
        $_GET['end'] = NULL;

// Load the last N minutes of messages from this channel
    $Channel->load_messages($_GET['start'], $_GET['end']);

// Load the names of the users currently logged into the channel
    $Channel->load_users($_GET['end']);

// Print the page
    require 'templates/channel.php';
