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
             $Channel->server->server,
             ':',
             $Channel->server->port,
             ' :: ',
             $Channel->channel;
?>
</h3>

<p>
<b>Current users:</b>
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
</p>

<p>
<a href="<?php echo root, 'channel/', $Channel->chanid, '/history' ?>">Daily chat history</a>
</p>

<table class="log">
<?php
    if (empty($Channel->messages)) {
        if ($start)
            echo "<tr><td>No activity was logged during the requested time period.</td></tr>";
        else
            echo "<tr><td>No activity has been logged in the last 15 minutes.</td></tr>";
    }
    else {
        $last_day = null;
        foreach ($Channel->messages as $message) {
        // Print out a nice separator between each day
            $day = date('l, F jS, Y', $message->timestamp);
            if ($day != $last_day) {
                $last_day = $day;
                echo "<tr class=\"log_line\">\n    <td class=\"log_day\" colspan=\"3\">$day</td>\n</tr>";
            }
        // Now print the normal row
?>
<tr class="log_line">
    <td class="log_timestamp">[<?php echo date('H:i:s', $message->timestamp) ?>]</td>
<?php
            if ($message->msgtype == MSG_NORMAL)
                echo '    <td class="log_nick nick_', $message->nick_color(),'">', $message->nick, ":</td>\n",
                     '    <td';
            else
                echo '    <td colspan="2"';
            echo ' class="', $message->class;
            if ($message->msgtype == MSG_ACTION)
                echo ' nick_', $message->nick_color();
            echo '">';
            switch ($message->msgtype) {
                case MSG_ACTION:
                    echo '** ', $message->nick, ' ', $message->message, ' **';
                    break;
                case MSG_NICK:
                    echo $message->nick, ' has changed nicks to ', $message->message;
                    break;
                case MSG_NORMAL:
                case MSG_TOPIC:
                case MSG_KICK:
                case MSG_MODE:
                case MSG_JOIN:
                case MSG_PART:
                case MSG_QUIT:
                default:
                    echo $message->message;
            }
    ?></td>
</tr>
<?php
        }
    }
?>
</table>

<?php
// Print the page footer
    require_once 'templates/footer.php';

