/**
 * A random assortment of javascript debug routines
 *
 * @url         $URL: svn+ssh://xris@svn.siliconmechanics.com/var/svn/web/trunk/shared_code/js/debug.js $
 * @date        $Date: 2005-11-21 11:17:03 -0800 (Mon, 21 Nov 2005) $
 * @version     $Revision: 1318 $
 * @author      $Author: xris $
 * @copyright   Silicon Mechanics
 * @license     LGPL
 *
 * @package     SiMech
 * @subpackage  Javascript
 *
/**/

    var debug_window_handle;
// Create a debug window and debug into it
    function debug_window(string) {
        if (!debug_window_handle || debug_window_handle.closed) {
            debug_window_handle = window.open('', 'Debug Window','scrollbars, resizable, width=400, height=600');
            debug_window_handle.document.write('<html><body style="font-size: 9pt; background-color: #f88;">');
        }
        debug_window_handle.document.write('<pre>'+string+'</pre><hr>');
    }
