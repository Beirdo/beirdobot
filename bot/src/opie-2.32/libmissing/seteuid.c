/* seteuid.c: A replacement for the seteuid function

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

int seteuid FUNCTION((euid), uid_t euid)
{
#if HAVE_SETREUID
  return setreuid(-1, euid);
#else /* HAVE_SETREUID */
#if HAVE_SETRESUID
  return setresuid(-1, euid, -1);
#else /* HAVE_SETRESUID */
#error Cannot build a seteuid() replacement.
#endif /* HAVE_SETRESUID */
#endif /* HAVE_SETREUID */
}
