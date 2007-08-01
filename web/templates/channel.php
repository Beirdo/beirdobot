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
    $page_title = 'Beirdobot, '.$Channel->server->server.' :: '.$Channel->channel;

// Custom headers
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/channel.css" >';
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/log.css" >';

// Print the page header
    require_once 'templates/header.php';
?>
<h3>
<?php
        echo $Channel->server->nick,
             '@',
             $Channel->server->server;
        if ($Channel->server->port != 6667)
            echo ':', $Channel->server->port;
        echo ' :: ', $Channel->channel;
?>
</h3>

<p>
<a href="<?php echo root, 'channel/', $Channel->chanid, '/history' ?>">Daily chat history</a>
</p>

<div id="current_users">
<h4>Current users (<?php echo count($Channel->users); ?>):</h4>
<?php
    if (empty($Channel->users)) {
        echo '<tr><td>No one is currently logged into ', $Channel->channel, "</td></tr>";
    }
    else {
        $users = array();
        foreach ($Channel->users as $user) {
            $users[] = $user['nick'];
        }
        echo implode(', ', $users);
    }
?>
</div>

<?php

    #timer();
    $Channel->print_log($start, $end);
    #echo timer('Logs rendered in %.4f seconds.');

?>

<?php
// Print the page footer
    require_once 'templates/footer.php';

