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
    $min = $db->query_col('SELECT timestamp
                             FROM irclog
                            WHERE chanid=?
                         ORDER BY timestamp ASC
                            LIMIT 1',
                         $Channel->chanid);
    $max = $db->query_col('SELECT timestamp
                             FROM irclog
                            WHERE chanid=?
                         ORDER BY timestamp DESC
                            LIMIT 1',
                         $Channel->chanid);

    $min = day_in_seconds * intVal(($min + date('Z')) / day_in_seconds) - date('Z');
    $max = day_in_seconds * intVal(($max + date('Z')) / day_in_seconds) - date('Z');

/**
 * @global  array    $GLOBALS['Years']
 * @name    $Years
/**/
    $Years = array();

// Start counting backwards
    while ($max >= $min) {
        $Years[date('Y', $max)][date('n', $max)][] = $max;
        $max -= day_in_seconds;
    }

// Display
    require 'templates/channel/history.php';

// Exit
    exit;
