<?php
/**
 * Perform a search
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
/**/

// Search string
    isset($_GET['s']) or $_GET['s'] = $_POST['s'];

// Our time division (15 minute blocks)
    $division = 15 * 60;

// Build the query
    $params = array($_GET['s'], $_GET['s']);
    $query  = "SELECT chanid,
                      $division * FLOOR(MIN(timestamp) / $division) AS starttime,
                      SUM(MATCH(nick, message) AGAINST (?)) AS score
                 FROM irclog
                WHERE msgtype IN (0, 1)
                      AND MATCH(nick, message) AGAINST (?) > 0";

// Extra parameters
    if (!empty($_GET['chanid'])) {
        $query   .= ' AND chanid=?';
        $params[] = intVal($_GET['chanid']);
    }

// Extra parameters
    if (!empty($_GET['starttime'])) {
        $query   .= ' AND timestamp>=?';
        $params[] = strtotime($_GET['starttime']);
    }
// Extra parameters
    if (!empty($_GET['endtime'])) {
        $query   .= ' AND timestamp>=?';
        $params[] = strtotime($_GET['endtime']);
    }

// Finish building the query
    $query .= " GROUP BY chanid, $division * FLOOR(timestamp / $division)
                ORDER BY score DESC, msgid ASC
                   LIMIT 100";

/**
 * @global  array    $GLOBALS['Results']
 * @name    $Results
/**/
    $Results = array();

// Start the timer
    $search_time = microtime();

// Run the query and gather the results
    $sh = $db->query($query, $params);
    while ($row = $sh->fetch_assoc()) {
        $row['channel']        = &$Channels[$row['chanid']];
        $row['server']         = &$row['channel']->server;
        $row['endtime']        = $row['starttime'] + $division;
        $row['link_starttime'] = $row['starttime'] - intVal($division / 2);
        $row['link_endtime']   = $row['starttime'] + intVal($division / 2);
        $Results[] = $row;
    }
    $sh->finish();

// How long
    $search_time = microtime() - $search_time;

// Print the page
    require 'templates/search.php';
