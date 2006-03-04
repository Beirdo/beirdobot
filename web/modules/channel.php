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

/**
 * @global  int     $GLOBALS['start']
 * @name    $start
/**/
    $start = null;
/**
 * @global  int     $GLOBALS['end']
 * @name    $end
/**/
    $end = null;

// Date?
    if (preg_match('/^(\d+)-(\d+)-(\d+)(?::(\d+):(\d+):(\d+))?$/', $Path[2], $match)) {
        $start = mktime(0, 0, 0, $match[2], $match[3], $match[1]);
        if (preg_match('/^(\d+)-(\d+)-(\d+)(?::(\d+):(\d+):(\d+))?$/', $Path[3], $match))
            $end = mktime(23, 59, 59, $match[2], $match[3], $match[1]);
        else {
            $end = $start + day_in_seconds - 1;
        }
    }

// No start time -- show the most recent 15 minutes (or so)
    if (!$start) {
        $start = 60 * intVal((time() - (15 * 60)) / 60);
    }
// No end date
    elseif ($start && (!$end || $start == $end)) {
        $end = $start + day_in_seconds - 1;
    }
// Out of order?
    elseif ($start > $end) {
        $tmp   = $start;
        $start = $end;
        $end   = $tmp;
    }

// Don't load more than a week at a time
    if ($end - $start > day_in_seconds * 7) {
        $end = $start + (day_in_seconds * 7) - 1;
        ### add an error explaining that the most you can pull is a week
    }

// Load the last N minutes of messages from this channel
    $Channel->load_messages($start, $end);

// Load the names of the users currently logged into the channel
    $Channel->load_users($end);

// Print the page
    require 'templates/channel.php';
