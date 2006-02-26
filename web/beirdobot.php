<?php
/**
 * The main brain script for all of Beirdobot.
 *
 * @url         $URL: svn+ssh://xris@cvs.mythtv.org/var/lib/svn/trunk/mythplugins/mythweb/mythweb.php $
 * @date        $Date: 2006-01-30 16:05:51 -0800 (Mon, 30 Jan 2006) $
 * @version     $Revision: 8793 $
 * @author      $Author: xris $
 * @license     GPL
 *
 * @package     MythWeb
 *
/**/

// Add a custom include path?
    if (!empty($_SERVER['include_path']) && $_SERVER['include_path'] != '.')
        ini_set('include_path', $_SERVER['include_path'].PATH_SEPARATOR.ini_get('include_path'));

// Init
    require_once 'includes/init.php';

// Standard module?  Pass along the
    if (file_exists(modules_path.'/'.$Path[0].'.php')) {
        require_once modules_path.'/'.$Path[0].'.php';
    }
    elseif (!empty($Path[0]) && preg_match('/\w/', $Path[0])) {
        $Error = 'An unknown module was requested:  '.$Path[0];
        require_once 'templates/_error.php';
    }
    else {
        require_once 'modules/welcome.php';
    }

// Exit gracefully
    exit;
