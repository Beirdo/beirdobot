<?php
/**
 * Utility routines used throughout mythweb
 *
 * @url         $URL$
 * @date        $Date: 2006-01-24 19:41:36 -0800 (Tue, 24 Jan 2006) $
 * @version     $Revision: 8709 $
 * @author      $Author: xris $
 * @license     GPL
 *
 * @package     MythWeb
 *
/**/

/**
 * Get or set a database setting.
/**/
    function setting($field, $new_value = "old\0old") {
        global $db;
        static $cache = array();
    // Assigning a new value
        if ($new_value !== "old\0old") {
            $db->query('REPLACE INTO settings (value, data) VALUES (?,?)',
                       $field, $new_value);
            $cache[$field] = $new_value;
        }
    // Not cached?
        elseif (!array_key_exists($field, $cache)) {
            $cache[$field] = $db->query_col('SELECT data FROM settings WHERE value=?',
                                            $field);
        }
    // Return the cached value
        return $cache[$field];
    }

/**
 *  I like how in perl, you can pass variables into functions in lists or
 *  arrays, and they all show up to the function as one giant list.  This takes
 *  an array containing scalars and arrays of scalars, and returns one clean
 *  array of all values.
/**/
    function smart_args($args) {
        $new_args = array();
    // Not an array
        if (!is_array($args))
            return array($args);
    // Loop
        foreach ($args as $arg) {
            foreach (smart_args($arg) as $arg2) {
                $new_args[] = $arg2;
            }
        }
    // Return
        return $new_args;
    }

/**
 * Recursively fixes silly \r\n stuff that some browsers send.
 * Also adds a generic entry for fiends ending in _x or _y to better deal
 * with image inputs.
/**/
    function &fix_crlfxy(&$array) {
        foreach ($array as $key => $val) {
			if (is_array($val))
				fix_crlfxy($array[$key]);
            elseif (is_string($val)) {
                $array[$key] = str_replace("\r\n", "\n", $val);
            // Process any imagemap submissions to make sure we also get the name itself
                if ($key != ($new_key = preg_replace('/_[xy]$/', '', $key))) {
                    if (!array_key_exists($new_key, $array))
                        $array[$new_key] = true;
                }
            }
        }
        return $array;
    }

/**
 * Recursively strip slashes from an array (eg. $_GET).
/**/
	function &fix_magic_quotes(&$array) {
		foreach ($array as $key => $val) {
			if (is_array($val))
				fix_magic_quotes($array[$key]);
			else
				$array[$key] = stripslashes($val);
		}
		return $array;
	}

/**
 * Print a redirect header and exit
/**/
    function redirect_browser($url) {
        header("Location: $url");
        echo "\n";
        exit;
    }

/**
 * Overloaded version of htmlentities() that requests the UTF-8 entities rather
 * than the default ISO-9660
 *
 * @param string $str   String to convert to html entities
 *
 * @return UTF-8 entities for $str
/**/
    function html_entities($str) {
        return htmlentities($str, ENT_COMPAT, 'UTF-8');
    }

/**
 * Returns a sorted list of files in a directory, minus . and ..
/**/
    function get_sorted_files($dir = '.', $regex = '', $negate = false) {
        $list = array();
        $handle = opendir($dir);
        while(false != ($file = readdir($handle))) {
            if ($file == '.' || $file == '..') continue;
            if (!$regex || (!$negate && preg_match($regex, $file)) || ($negate && !preg_match($regex, $file)))
                $list[] = $file;
        }
        closedir($handle);
        sort($list);
        return $list;
    }

/**
 * Find a particular file in the current include_path
 *
 * @param        string     $file       Name of the file to look for
 * @return       mixed      Full path to the requested file, or null if it isn't found.
/**/
    function find_in_path($file) {
    // Split out each of the search paths
        foreach (explode(PATH_SEPARATOR, ini_get('include_path')) as $path) {
        // Formulate the absolute path
            $full_path = $path . DIRECTORY_SEPARATOR . $file;
        // Exists?
            if (file_exists($full_path))
                return $full_path;
        }
        return null;
    }

/**
 * returns $this or $or_this
 * if $gt is set to true, $this will only be returned if it's > 0
 * if $gt is set to a number, $this will only be returned if it's > $gt
/**/
    function _or($this, $or_this, $gt = false) {
        if ($gt === true)
            return $this > 0 ? $this : $or_this;
        if (!empty($gt))
            return $this > $gt ? $this : $or_this;
        return $this ? $this : $or_this;
    }

/**
 * DEBUG:
 *  prints out a piece of data
/**/
    function debug($data, $file = false, $force = false) {
        if(!dev_domain && !$force)
            return;
        static $first_run=true;
        if($first_run) {
            $first_run=false;
            echo '<script type="text/javascript" src="/js/debug.js"></script>';
        }
    // Put our data into a string
        if (is_array($data) || is_object($data))
            $str = print_r($data, TRUE);
        elseif (isset($data))
            $str = $data;
        $search = array("\n", '"');
        $replace = array("\\n", '\"');
        $back_trace = debug_backtrace();
    // If this is a string, int or float
        if (is_string($str) || is_int($str) || is_float($str)) {
        // Allow XML/HTML to be treated as normal text
            $str = htmlspecialchars($str, ENT_NOQUOTES);
        }
    // If this is a boolean
        elseif (is_bool($str))
            $str = $str ? '<i>**TRUE**</i>' : '<i>**FALSE**</i>';
    // If this is null
        elseif (is_null($str))
            $str = '<i>**NULL**</i>';
    // If it is not a string, we return a get_type, because it would be hard to generically come up with a way
    // to display anything
        else
            $str = '<i>Type : '.gettype($str).'</i>';
    // Show which line caused the debug message
        $str = $str."\n<hr>\n".'Line #'.$back_trace[0]['line'].' in file '.$back_trace[0]['file']."\n";
    // Print the message
        echo '<script language="javascript">debug_window("'.str_replace($search, $replace, $str).'");</script>';
        echo '<noscript><pre>'.$str.'</pre></noscript>';
    // Print to a file?
        if ($file) {
            $out = fopen('/tmp/debug.txt', 'a');
            fwrite($out, "$str\n");
            fclose($out);
        }
    }

