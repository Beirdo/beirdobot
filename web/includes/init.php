<?php
/**
 * This file is part of MythWeb, a php-based interface for MythTV.
 * See http://www.mythtv.org/ for details.
 *
 * Initialization routines.  This file basically loads all of the necessary
 * shared files for the entire program.
 *
 * @url         $URL$
 * @date        $Date: 2006-02-01 21:36:08 -0800 (Wed, 01 Feb 2006) $
 * @version     $Revision: 8832 $
 * @author      $Author: xris $
 * @license     GPL
 *
 * @package     MythWeb
 *
/**/

// mod_redirect can do some weird things when php is run in cgi mode
    $keys = preg_grep('/^REDIRECT_/', array_keys($_SERVER));
    if (!empty($keys)) {
        foreach ($keys as $key) {
            $key = substr($key, 9);
            if (!array_key_exists($key, $_SERVER))
                $_SERVER[$key] = $_SERVER["REDIRECT_$key"];
        }
    }

// Clean the document root variable and make sure it doesn't have a trailing slash
    $_SERVER['DOCUMENT_ROOT'] = preg_replace('/\/+$/', '', $_SERVER['DOCUMENT_ROOT']);

// Are we running in SSL mode?
    define('is_ssl', ($_SERVER['SERVER_PORT'] == 443 || !empty($_SERVER['SSL_PROTOCOL']) || !empty($_SERVER['HTTPS']))
                     ? true
                     : false);

// Figure out the root path for this mythweb installation.  We need this in order
// to cleanly reference things like the /js directory from subpaths.
    define('root', str_replace('//', '/', dirname($_SERVER['SCRIPT_NAME']).'/'));;

// Several sections of this program require the current hostname
    define('hostname', empty($_SERVER['hostname']) ? trim(`hostname`) : $_SERVER['hostname']);

// Load the generic utilities so we have access to stuff like DEBUG()
    require_once 'includes/utils.php';

// Load the error trapping and display routines
    require_once 'includes/errors.php';
    require_once 'includes/errordisplay.php';

// Make sure we're running a new enough version of php
    if (substr(phpversion(), 0, 3) < 4.3)
        trigger_error('You must be running at least php 4.3 to use this program.', FATAL);

// Clean up input data
    fix_crlfxy($_GET);
    fix_crlfxy($_POST);
    if (get_magic_quotes_gpc()) {
        fix_magic_quotes($_COOKIE);
        fix_magic_quotes($_ENV);
        fix_magic_quotes($_GET);
        fix_magic_quotes($_POST);
        fix_magic_quotes($_REQUEST);
        fix_magic_quotes($_SERVER);
    }

// No MySQL libraries installed in PHP
    if (!function_exists('mysql_connect')) {
        $Error = "Please install the MySQL libraries for PHP.\n"
                .'The package is usually called something like php-mysql.';
        require_once 'templates/_error.php';
        exit;
    }

// No database connection info defined?
    if (empty($_SERVER['db_server']) || empty($_SERVER['db_name']) || empty($_SERVER['db_login'])) {
        $Error = '<p>
The database environment variables are not correctly set in the<br />
included .htaccess file.  Please read through the comments included<br />
in the file and set up the db_* environment variables correctly.
</p>
<p>
Some possible solutions are to make sure that mod_env is enabled<br />
in httpd.conf, as well as having followed the instructions in the<br />
README about the AllowOverride settings.
</p>';
        require_once 'templates/_error.php';
        exit;
    }

/**
 * $Path is an array of PATH_INFO passed into the script via mod_rewrite or some
 * other lesser means.  It contains most of the information required for
 * figuring out what functions the user wants to access.
 *
 * @global  array   $GLOBALS['Path']
 * @name    $Path
/**/
    global $Path;
    $Path = explode('/', preg_replace('/^\/+/',   '',    // Remove leading slashes
                         preg_replace('/[\s]+/', ' ',    // Convert extra whitespace
                                                         // Grab the path info from various different places.
                             array_key_exists('PATH_INFO', $_SERVER)
                             && $_SERVER['PATH_INFO']
                                ? $_SERVER['PATH_INFO']
                                : (array_key_exists('PATH_INFO', $_ENV)
                                   && $_ENV['PATH_INFO']
                                    ? $_ENV['PATH_INFO']
                                    : $_GET['PATH_INFO']
                                  )
                         ))
                   );

// Load the database connection routines
    require_once 'includes/db.php';

/**
 * All database connections should now go through this object.
 *
 * @global  Database    $GLOBALS['db']
 * @name    $db
/**/
    global $db;

// Connect to the database
    $db = new Database($_SERVER['db_name'],
                       $_SERVER['db_login'],
                       $_SERVER['db_password'],
                       $_SERVER['db_server']);

// Access denied -- probably means that there is no database
    if ($db->errno == 1045) {
        require_once 'templates/_db_access_denied.php';
        exit;
    }

// We don't need these security risks hanging around taking up memory.
    unset($_SERVER['db_name'],
          $_SERVER['db_login'],
          $_SERVER['db_password'],
          $_SERVER['db_server']);

//
//  If there was a database connection error, this will send an email to
//    the administrator, and then present the user with a static page
//    informing them of the trouble.
//
    if ($db->error) {
    // Notify the admin that the database is offline!
        if (strstr(error_email, '@'))
            mail(error_email, "Database Connection Error" ,
                 $db->error,
                 'From:  PHP Error <php_errors@'.server_domain.">\n");
    // Let the user know that something's wrong
        $Error = "Database Error:<br/><br/>\n".$db->error;
        require_once 'templates/_error.php';
        exit;
    }

// Find the modules path
    $path = find_in_path('modules/welcome.php');
    if ($path) {
        define('modules_path', dirname($path));
    }
    else {
        $Error = 'Could not find the modules directory, or there are no modules installed.';
        require_once 'templates/_error.php';
        exit;
    }

// Load the session handler routines
#    require_once 'includes/session.php';

// Is there a preferred skin?
    if (file_exists('skins/'.$_SESSION['skin'].'/img/') && !$_REQUEST['RESET_SKIN']) {
        define('skin', $_SESSION['skin']);
    }
    else {
        define('skin', 'default');
    }
    $_SESSION['skin'] = skin;

// Set up some handy constants
    define('skin_dir', 'skins/'.skin);
    define('skin_url', root.skin_dir);

// Make sure the data directories exist and are writable
    foreach (array('data', 'data/static') as $dir) {
        if (!is_dir($dir) && !mkdir($dir, 0755)) {
            $Error = "Error creating directory $dir/ . Please check permissions.";
            require_once 'templates/_error.php';
            exit;
        }
        if (!is_writable($dir)) {
            $process_user = posix_getpwuid(posix_geteuid());
            $Error = "$dir is not writable by ".$process_user['name'].'. Please check permissions.';
            require_once 'templates/_error.php';
            exit;
        }
    }

// Load the IRC classes
    require_once 'includes/irc_messages.php';
    require_once 'includes/irc_servers.php';
    require_once 'includes/irc_channels.php';

