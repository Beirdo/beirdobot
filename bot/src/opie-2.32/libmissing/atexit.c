/* atexit.c: A replacement for the atexit function

%%% copyright-cmetz-97
This software is Copyright 1998 by Craig Metz, All Rights Reserved.
The Inner Net License Version 2 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.31. Changed error text to be more
		friendly to some compilers.
	Created by cmetz for OPIE 2.3.
*/
#include "opie_cfg.h"
#include "opie.h"

VOIDRET atexit(function)
VOIDRET (*function)(void);
{
#if HAVE_ON_EXIT
  on_exit(function, NULL);
#else /* HAVE_ON_EXIT */
#error No functions available with which to build an atexit() replacement.
#endif /* HAVE_ON_EXIT */
}
