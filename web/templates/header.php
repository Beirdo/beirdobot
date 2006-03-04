<?php
/**
 * HTML Header
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
/**/
?>
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
    <title><?php echo $page_title ?></title>

    <script type="text/javascript" src="<?php echo root ?>js/utils.js"></script>
    <script type="text/javascript" src="<?php echo root ?>js/browser.js"></script>
    <script type="text/javascript" src="<?php echo root ?>js/visibility.js"></script>
    <script type="text/javascript" src="<?php echo root ?>js/ajax.js"></script>

    <link rel="stylesheet" type="text/css" href="<?php echo skin_url ?>/global.css" >
<?php
    if (!empty($headers))
        echo "\n    ", implode("\n    ", $headers), "\n";
?>
</head>

<body>

<p>header...</p>
<hr >

