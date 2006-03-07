<tr class="log_line">
    <td class="log_day" colspan="3"><?php
        echo date($last_day ? 'l, F jS, Y' : 'l, F jS, Y, H:i T',
                  $message->timestamp)
        ?></td>
</tr>
