<tr class="log_line">
    <td class="log_timestamp">[<?php echo date('H:i:s', $message->timestamp) ?>]</td>
    <?php
        if ($message->msgtype == MSG_NORMAL)
            echo '<td class="log_nick nick_', $message->nick_color(),'">', $message->nick, ":</td>\n",
                 '    <td';
        else
            echo '<td colspan="2"';
        echo ' class="', $message->class;
        if ($message->msgtype == MSG_ACTION)
            echo ' nick_', $message->nick_color();
        echo '">';
        switch ($message->msgtype) {
            case MSG_ACTION:
                echo '** ', $message->nick, ' ', $message->message, ' **';
                break;
            case MSG_NICK:
                echo $message->nick, ' is now known as ', $message->message;
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
