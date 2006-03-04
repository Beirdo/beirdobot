<?php
/**
 * Display file for the main beirdobot welcome page
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
/**/
// Set the desired page title
    $page_title = 'Beirdobot, search results';

// Custom headers
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/search.css" >';
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/log.css" >';

// Print the page header
    require_once 'templates/header.php';
?>

<h3>Search Results:</h3>

<table class="results">
<?php
    if (empty($Results))
        echo "<tr><td>No matches found.</td></tr>\n";
    else {
        echo "<tr>\n",
             "    <th class=\"score\">score</th>\n",
             "    <th class=\"server\">server</th>\n",
             "    <th class=\"channel\">channel</th>\n",
             "    <th class=\"date\">date</th>\n",
             "    <th class=\"time\">time</th>\n",
             "</tr>";
        foreach ($Results as $result) {
            echo "<tr>\n    <td class=\"score\">",  round($result['score'], 2),
                 "</td>\n    <td class=\"server\">", $result['channel']->server->server;
            if ($result['channel']->server->port != 6667)
                echo ':', $result['channel']->server->port;
            echo "</td>\n    <td class=\"channel\">",
                 $result['channel']->channel,
                 "</td>\n    <td class=\"date\"><a href=\"",
                                                root, 'channel/', $result['channel']->chanid,
                                                '/', date('Y-m-d', $result['starttime']),
                                                '">',
                 date('l, F jS, Y', $result['starttime']),
                 "</a></td>\n    <td class=\"time\"><a href=\"",
                                                    root, 'channel/', $result['channel']->chanid,
                                                    '/', date('Y-m-d:H:i', $result['link_starttime']),
                                                    '/', date('Y-m-d:H:i', $result['link_endtime']),
                                                    '">',
                 date('H:i', $result['starttime']),
                 ' to ',
                 date('H:i T', $result['endtime']),
                 "</a></td>\n</tr>";
        }
    }
?>
</table>

<p>
Search took <?php echo $search_time ?> seconds.
</p>

<?php
// Print the page footer
    require_once 'templates/footer.php';
