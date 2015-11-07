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

/* xfiles.c - General support routines for xfiles */
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "xfiles.h"
#include "xf_errs.h"


extern afs_uint32 xfon_path(XFILE *, int, char *);
extern afs_uint32 xfon_fd(XFILE *, int, char *);
extern afs_uint32 xfon_voldump(XFILE *, int, char *);
extern afs_uint32 xfon_profile(XFILE *, int, char *);
extern afs_uint32 xfon_stdio(XFILE *, int);

struct xftype {
  struct xftype *next;
  char *name;
  afs_uint32 (*do_on)(XFILE *, int, char *);
};


static struct xftype *xftypes = 0;
static int did_register_defaults = 0;


afs_uint32 xfregister(char *name, afs_uint32 (*do_on)(XFILE *, int, char *))
{
  struct xftype *x;

  if (!(x = (struct xftype *)malloc(sizeof(struct xftype)))) return ENOMEM;
  memset(x, 0, sizeof(*x));
  x->next = xftypes;
  x->name = name;
  x->do_on = do_on;
  xftypes = x;
  return 0;
}


static void register_default_types(void)
{
  xfregister("FILE",    xfon_path);
  xfregister("FD",      xfon_fd);
  xfregister("AFSDUMP", xfon_voldump);
  xfregister("PROFILE", xfon_profile);
  did_register_defaults = 1;
}


afs_uint32 xfopen(XFILE *X, int flag, char *name)
{
  struct xftype *x;
  char *type, *sep;

  if (!did_register_defaults) register_default_types();
  if (!strcmp(name, "-")) return xfon_stdio(X, flag);

  for (type = name; *name && *name != ':'; name++);
  if (*name) {
    sep = name;
    *name++ = 0;
  } else {
    sep = 0;
    name = type;
    type = "FILE";
  }

  for (x = xftypes; x; x = x->next)
    if (!strcmp(type, x->name)) break;
  if (sep) *sep = ':';
  if (x) return (x->do_on)(X, flag, name);
  return ERROR_XFILE_TYPE;
}
