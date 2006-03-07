<tr>
    <td><?php
        if ($end)
            echo 'No activity was logged during the requested time period.';
        else
            echo 'No activity has been logged in the last ',
                 intVal((time() - $start) / 60),
                 ' minutes.';
        ?></td>
</tr>
