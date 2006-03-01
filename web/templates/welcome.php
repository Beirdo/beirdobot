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
    $page_title = 'Welcome to Beirdobot!';

// Custom headers
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/welcome.css" />';

// Print the page header
    require_once 'templates/header.php';
?>
The following servers/channels are being monitored:
<ul>
<?php
foreach ($Servers as $server) {
    echo '    <li>',
         $server->nick,
         '@',
         $server->server,
         ':',
         $server->port,
         "\n        <ul>\n";
// Channels
    if (empty($server->channels)) {
        echo "    <li><i>no logged channels</i></li>\n";
    }
    else {
        foreach ($server->channels as $channel) {
            echo '    <li><a href="'.root.'channel/'.$channel->chanid.'">'.$channel->channel,
                 '</a> ('.date('Y-m-d H:m:s', $channel->first_entry).")</li>\n";
        }
    }
    echo "        </ul>\n        </li>\n";
}
?>
</ul>

<?php
// Print the page footer
    require_once 'templates/footer.php';

