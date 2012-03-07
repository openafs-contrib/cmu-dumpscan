/*
 * CMUCS AFStools
 * dumpscan - routines for scanning and manipulating AFS volume dumps
 *
 * Copyright (c) 1998, 2001 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software_Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/* xf_profile_name.c - profile module open-by-name functions */

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include "xfiles.h"
#include "xf_errs.h"


/* Open a profiled XFILE */
extern afs_uint32 xf_PROFILE_do_open(XFILE *, int, char *,
                                     XFILE *, int, XFILE *, int);


afs_uint32 xfopen_profile_to(XFILE *X, int flag, XFILE *cX, char *profile)
{
  XFILE *pX;
  afs_uint32 err;

  pX = malloc(sizeof(XFILE));
  if (!pX) return ENOMEM;

  err = xfopen(pX, O_RDWR|O_CREAT|O_TRUNC, profile);
  if (err) {
    free(pX);
    return err;
  }

  return xf_PROFILE_do_open(X, flag, "<X>", cX, 0, pX, 1);
}

afs_uint32 xfopen_profile_name(XFILE *X, int flag, char *content, XFILE *pX)
{
  XFILE *cX;
  afs_uint32 err;

  cX = malloc(sizeof(XFILE));
  if (!cX) return ENOMEM;

  err = xfopen(cX, flag, content);
  if (err) {
    free(cX);
    return err;
  }

  return xf_PROFILE_do_open(X, flag, content, cX, 1, pX, 0);
}

afs_uint32 xfopen_profile_name_to(XFILE *X, int flag,
                                  char *content, char *profile)
{
  XFILE *pX, *cX;
  afs_uint32 err;

  pX = malloc(sizeof(XFILE));
  if (!pX) return ENOMEM;

  err = xfopen(pX, O_RDWR|O_CREAT|O_TRUNC, profile);
  if (err) {
    free(pX);
    return err;
  }

  cX = malloc(sizeof(XFILE));
  if (!cX) {
    xfclose(pX);
    free(pX);
    return ENOMEM;
  }

  err = xfopen(cX, flag, content);
  if (err) {
    xfclose(pX);
    free(pX);
    free(cX);
    return err;
  }

  return xf_PROFILE_do_open(X, flag, content, cX, 1, pX, 1);
}


afs_uint32 xfon_profile(XFILE *X, int flag, char *name)
{
  char *x, *profile, *xname;
  afs_uint32 err;

  if (!(name = strdup(name))) return ENOMEM;

  profile = "-";
  xname = name;
  for (x = name; *x; x++) {
    if (x[0] == ':' && x[1] == ':') {
      *x = 0;
      profile = name;
      xname = x + 2;
      break;
    }
  }
  if (!*name) profile = "-";
  err = xfopen_profile_name_to(X, flag, xname, profile);
  free(name);
  return err;
}
