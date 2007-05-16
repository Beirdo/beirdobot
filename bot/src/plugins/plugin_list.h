/*
 *  This file is part of the beirdonet package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  The plugin portion of beirdobot is free software; you can redistribute it 
 *  and/or modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this software; if not, write to the 
 *    Free Software Foundation, Inc., 
 *    51 Franklin Street, Fifth Floor,
 *    Boston, MA  02110-1301  USA
 */

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2006 Gavin Hurlbut
* All rights reserved
*
*/

#ifndef plugin_list_h_
#define plugin_list_h_

#include "environment.h"
#include "structs.h"

/* CVS generated ID string (optional for h files) */
static char plugin_list_h_ident[] _UNUSED_ = 
    "$Id$";

#ifdef _DEFINE_PLUGINS
static PluginDef_t DefaultPlugins[] = {
    { "trout",      "plugin_trout.so",      1,  "" },
    { "fart",       "plugin_fart.so",       0,  "" },
    { "core",       "plugin_core.so",       1,  "" },
    { "dns",        "plugin_dns.so",        1,  "" },
    { "rssfeed",    "plugin_rssfeed.so",    0,  "" },
    { "luascript",  "plugin_luascript.so",  0,  "" },
};
static int DefaultPluginCount = NELEMENTS(DefaultPlugins);
#endif

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
