<?php
/**
 * Prints a nice list of the channel history
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
/**/

// Pull up the earliest and latest log entries
    list($min, $max) = $db->query_row('SELECT MIN(timestamp), MAX(timestamp)
                                         FROM irclog
                                        WHERE chanid=?
                                     GROUP BY chanid',
                                      $Channel->chanid);

// One day, in seconds
    define('day_in_seconds',  60 * 60 * 24);

// Round back to the nearest day (adjust for timezone differences)
    $min = day_in_seconds * intVal($min / day_in_seconds) - date('Z');
    $max = day_in_seconds * intVal($max / day_in_seconds) - date('Z');


/**
 * @global  array    $GLOBALS['Years']
 * @name    $Years
/**/
    $Years = array();

// Start counting
    while ($min <= $max) {
        $Years[date('Y')][date('n')][] = $min;
        $min += day_in_seconds;
    }

// Display
    require 'templates/channel/history.php';

// Exit
    exit;
