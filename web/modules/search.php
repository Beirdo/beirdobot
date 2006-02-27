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
    isset($_GET['s']) or $_GET['s'] = $_POS['s'];

// Our time division
    $division = 15 * 60;

// Build the query
    $params = array($_GET['s'], $_GET['s']);
    $query  = "SELECT chanid, msgid, nick, message,
                      $division * FLOOR(MIN(timestamp) / $division) AS timestamp,
                      SUM(MATCH(nick, message) AGAINST (?)) AS score
                 FROM irclog
                WHERE msgtype IN (0, 1)
                      AND MATCH(nick, message) AGAINST (?) > 0";

// Extra parameters
    if (!empty($_GET['chanid'])) {
        $query   .= ' AND chanid=?';
        $params[] = $_GET['chanid'];
    }

// Finish building the query
    $query .= " GROUP BY chanid, $division * FLOOR(timestamp / $division)
                ORDER BY score DESC, msgid ASC";

/**
 * @global  array    $GLOBALS['Results']
 * @name    $Results
/**/
    $Results = array();

// Run the query and gather the results
    $sh = $db->query($query, $params);
    while ($row = $sh->fetch_assoc()) {
        $Results[] = $row;
    }
    $sh->finish();

