/* setegid.c: A replacement for the setegid function

%%% copyright-cmetz-97
This software is Copyright 1998 by Craig Metz, All Rights Reserved.
The Inner Net License Version 2 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Created by cmetz for OPIE 2.31.
*/
#include "opie_cfg.h"
#include <sys/types.h>
#include "opie.h"

int setegid FUNCTION((egid), uid_t egid)
{
#if HAVE_SETREGID
  return setregid(-1, egid);
#else /* HAVE_SETREGID */
#if HAVE_SETRESGID
  return setresgid(-1, egid, -1);
#else /* HAVE_SETRESGID */
#error Cannot build a setegid() replacement.
#endif /* HAVE_SETRESGID */
#endif /* HAVE_SETREGID */
}
