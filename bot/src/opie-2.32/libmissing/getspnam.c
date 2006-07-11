/* getspnam.c: A getspnam() function for SunOS C2 shadow passwords

%%% copyright-cmetz-96
This software is Copyright 1996-1998 by Craig Metz, All Rights Reserved.
The Inner Net License Version 2 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Created by cmetz for OPIE 2.31.
*/

#include "opie_cfg.h"
#include <stdio.h>
#include <pwd.h>
#include "opie.h"

static buffer[64];
static struct spwd spwd;

struct spwd *getspnam FUNCTION((name), char *name)
{
  struct passwd *passwd;

  if (!(passwd = getpwnam(name)))
    return NULL;

  if ((passwd->pw_passwd[0] != '#') || (passwd->pw_passwd[1] != '#') || !passwd->pw_passwd) {
    spwd.sp_pwdp = passwd->pw_passwd;
    return &spwd;
  }

  endpwent();

  {
    FILE *f;
    char *c, *c2;

    if (!(f = __opieopen("/etc/security/passwd.adjunct", 0, 0600)))
      return 0;

    while(fgets(buffer, sizeof(buffer), f)) {
      if (!(c = strchr(buffer, ':')))
	continue;
      *(c++) = 0;
      if (strcmp(buffer, name))
	continue;

      fclose(f);
      if (c2 = strchr(c, ':'))
	*c2 = 0;
      spwd.sp_pwdp = c;
      return &spwd;
    };

    fclose(f);
  };
  return NULL;
};
