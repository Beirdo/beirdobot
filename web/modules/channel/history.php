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

    $min = day_in_seconds * intVal(($min + date('Z')) / day_in_seconds) - date('Z');
    $max = day_in_seconds * intVal(($max + date('Z')) / day_in_seconds) - date('Z');

/**
 * @global  array    $GLOBALS['Years']
 * @name    $Years
/**/
    $Years = array();

// Start counting
    while ($min <= $max) {
     #   echo date('Y-m-d H:i:s', $min).' &lt;= '.date('Y-m-d H:i:s', $max)."<br/>";
    // Round back to the nearest day and adjust for timezone differences.
        #$t = day_in_seconds * intVal($min / day_in_seconds) - date('Z');
        #$Years[date('Y', $t)][date('n', $t)][] = $t;
        $Years[date('Y', $min)][date('n', $min)][] = $min;
    // On to the next
        $min += day_in_seconds;
    }

// Display
    require 'templates/channel/history.php';

// Exit
    exit;
