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
    $headers[] = '<link rel="stylesheet" type="text/css" href="'.skin_url.'/channel.css" />';

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
        echo "<tr><td>No one is currently logged into ".$Channel->channel."</td></tr>";
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

<table border="1">
<?php
    if (empty($Channel->messages)) {
        if ($_GET['start'])
            echo "<tr><td>No activity was logged during the requested time period.</td></tr>";
        else
            echo "<tr><td>No activity has been logged in the last 15 minutes.</td></tr>";
    }
    else {
        foreach ($Channel->messages as $message) {
?>
<tr>
    <td nowrap valign="top"><?php echo $message->timestamp ?></td>
    <td nowrap valign="top"><?php echo date('Y-m-d H:i:s', $message->timestamp) ?></td>
<?php
            switch ($message->msgtype) {
                case MSG_NORMAL:
                    echo '<td nowrap align="right" valign="top">', $message->nick,
                         ":</td>\n    <td>", $message->message, '</td>';
                    break;
                case MSG_ACTION:
                    echo '<td colspan="2"><b>** ', $message->nick, ' ', $message->message, ' **</b></td>';
                    break;
                case MSG_TOPIC:
                case MSG_KICK:
                case MSG_MODE:
                case MSG_NICK:
                case MSG_JOIN:
                case MSG_PART:
                case MSG_QUIT:
                default:
                    echo '<td colspan="2"><i>', $message->message, '</i></td>';
            }
            echo "\n";
?>
</tr>
<?php
        }
    }
?>
</table>

<?php
// Print the page footer
    require_once 'templates/footer.php';

