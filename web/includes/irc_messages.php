<?php
/**
 * IRC Messages
 *
 * @url         $URL$
 * @date        $Date$
 * @version     $Revision$
 * @author      $Author$
 *
 * @package     Beirdobot
 *
 * @uses        includes/irc_channels.php
 * @uses        $Channels
 *
/**/

// Define some message-related contstants
    define('MSG_NORMAL',  0);
    define('MSG_ACTION',  1);
    define('MSG_UNKNOWN', 2);
    define('MSG_UNKNOWN', 3);
    define('MSG_MODE',    4);
    define('MSG_UNKNOWN', 5);
    define('MSG_JOIN',    6);
    define('MSG_LEAVE',   7);
    define('MSG_SYSTEM',  8);

/**
 * Class to hold an individual IRC message
/**/
class irc_message {

    var $msgid;
    var $chanid;
    var $timestamp;
    var $nick;
    var $msgtype;
    var $raw_message;
    var $message;

    var $channel;

/**
 * Object constructor
 *
 * @param array $msg_vars   Hash of msg vars from the database.
/**/
    function __construct($msg_vars) {
        global $Channels;
    // Assign the various message vars
        $this->msgid        = $msg_vars['msgid'];
        $this->chanid       = $msg_vars['chanid'];
        $this->timestamp    = $msg_vars['timestamp'];
        $this->nick         = $msg_vars['nick'];
        $this->msgtype      = $msg_vars['msgtype'];
        $this->raw_message  = $msg_vars['message'];
    // Keep a reference to this channel's server
        $this->channel      =& $Channels[$this->chanid];
    // Make a printable version of the message text
        $this->parse_message();
    }

/**
 * Placeholder constructor for php4 compatibility
 *
 * @param array $msg_vars   Hash of msg vars from the database.
/**/
    function &irc_message($msg_vars) {
        return $this->__construct($msg_vars);
    }

/**
 *
 *
 * @param
 *
 * @return
/**/
    function parse_message() {
        $this->message = htmlentities($this->raw_message);
    // Only interact with certain kinds of messages
        if (!in_array($this->msgtype, array(MSG_NORMAL, MSG_ACTION)))
            return;
    // Perform some basic tag and text substitutions
		static $reg = array(
                        // Add links to url's
                            '#(\w+://[^\s<>]+)#'
                                => '<a href="$1">$1</a>',
                        // Add links to email addresses (does only a basic scan for valid email address formats)
                            '#((?:
                                  (?:[^<>\(\)\[\]\\.,;:\s@\"]+(?:\.[^<>\(\)\[\]\\.,;:\s@\"]+)*)
                                  |
                                  (?:"[^"]+")
                                ) @ (?:
                                  (?:\[[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\])
                                  |
                                  (?:[a-z\-0-9]+\.)+[a-z]+
                              ))#xe'
                                => 'encoded_mailto(\'$1\')',
                        // french spaces, last one Guillemet-left only if there is something before the space
                            '/(.) (?=\\?|:|;|!|\\302\\273)/' => '\\1&nbsp;\\2',
                        // french spaces, Guillemet-right
                            '/(\\302\\253) /'        => '\\1&nbsp;',
                        // Unescape special characters  (yes, the \\\\ actually matches a single \ character)
                            '/^\\\\(?=[*#;])/m'      => '',
                        // Insert htmlized versions of various dashes
                            '/ - /'                    => '&nbsp;&ndash; ', // N dash
                            '/(?<=\d)-(?=\d)(?!\d+-)/' => '&ndash;',        // N dash between individual sequences of numbers
                            '/ -- /'                   => '&nbsp;&mdash; ', // M dash
                           );
    // Perform the replacements
        $this->message = preg_replace(array_keys($reg), array_values($reg),
                                      $this->message);
    }
}

