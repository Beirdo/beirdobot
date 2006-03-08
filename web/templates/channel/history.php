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
// Set the desired page title
    $page_title = 'Beirdobot, '.$Channel->server->server.' :: '.$Channel->channel;

// Custom headers
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/channel.css" >';

// Print the page header
    require_once 'templates/header.php';
?>
<h3>
<?php
        echo $Channel->server->nick,
             '@',
             $Channel->server->server,
             ':',
             $Channel->server->port,
             ' :: ',
             $Channel->channel;
?>
</h3>

<p>
<a href="<?php echo root, 'channel/', $Channel->chanid ?>">Current activity</a>
</p>

<p>
<b>Channel history:</b>
</p>
<ul>
<?php
    foreach ($Years as $year => $months) {
        echo "    <li>$year\n        <ul>\n";
        foreach ($months as $month => $days) {
            echo '            <li>', date('F', $days[0]), "\n",
                 "                <ul>\n";
            foreach ($days as $day) {
                echo '                <li><a href="'.root.'channel/',
                     $Channel->chanid, '/', date('Y-m-d', $day),
                     '">', date('Y-m-d</\a> (l)', $day), "</li>\n";
            }
            echo "                </ul></li>\n";
        }
        echo "        </ul></li>\n";
    }
?>
</ul>

<?php
// Print the page footer
    require_once 'templates/footer.php';

