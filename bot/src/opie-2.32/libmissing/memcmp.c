/* strncasecmp.c: A replacement for the strncasecmp function

%%% copyright-cmetz-96
This software is Copyright 1996-1998 by Craig Metz, All Rights Reserved.
The Inner Net License Version 2 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Created by cmetz for OPIE 2.2.
*/
#include "opie_cfg.h"
#include "opie.h"

int memcmp FUNCTION((s1, s2, n), unsigned char *s1 AND unsigned char *s2 AND int n)
{
	while(n--) {
		if (*s1 != *s2)
			return (*s1 > *s2) ? 1 : -1;
		s1++;
		s2++;
	}
	return 0;
}
