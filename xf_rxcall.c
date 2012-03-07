/*
 * CMUCS AFStools
 * dumpscan - routines for scanning and manipulating AFS volume dumps
 *
 * Copyright (c) 1998, 2001, 2003 Carnegie Mellon University
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

/* xf_rxcall.c - XFILE routines for Rx bulk data transfers */

#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>

#include "xfiles.h"
#include "xf_errs.h"

#include <rx/xdr.h>
#include <rx/rx.h>
#include <rx/rx_null.h>
#include <rx/rxkad.h>
#include <afs/auth.h>
#include <afs/cellconfig.h>
#include <afs/vlserver.h>
#include <afs/volser.h>

#ifndef AFSCONF_CLIENTNAME
#include <afs/dirpath.h>
#define AFSCONF_CLIENTNAME AFSDIR_CLIENT_ETC_DIRPATH
#endif

#define O_MODE_MASK (O_RDONLY | O_WRONLY | O_RDWR)

struct rxinfo {
  struct rx_call *call;        /* call */
  afs_uint32 code;             /* result code */
  int writemode;               /* set if connection is write-only */
};

static afs_uint32 xf_rxcall_do_read(XFILE *X, void *buf, afs_uint32 count)
{
  struct rxinfo *i = X->refcon;
  afs_uint32 xcount;

  if (i->writemode) return ERROR_XFILE_WRONLY;
  xcount = rx_Read(i->call, buf, count);
  if (xcount == count) return 0;
  i->code = rx_Error(i->call);
  return i->code ? i->code : ERROR_XFILE_EOF;
}


static afs_uint32 xf_rxcall_do_write(XFILE *X, void *buf, afs_uint32 count)
{
  struct rxinfo *i = X->refcon;
  afs_uint32 xcount;

  xcount = rx_Write(i->call, buf, count);
  if (xcount == count) return 0;
  i->code = rx_Error(i->call);
  return i->code ? i->code : ERROR_XFILE_EOF;
}


static afs_uint32 xf_rxcall_do_close(XFILE *X)
{
  struct rxinfo *i = X->refcon;
  afs_uint32 code;

  code = i->code;
  free(i);
  return code;
}


afs_uint32 xfopen_rxcall(XFILE *X, int flag, struct rx_call *call)
{
  struct rxinfo *i;

  flag &= O_MODE_MASK;
  memset(X, 0, sizeof(*X));
  if (!(i = (struct rxinfo *)malloc(sizeof(struct rxinfo)))) return ENOMEM;
  i->call = call;
  i->code = 0;
  X->do_read  = xf_rxcall_do_read;
  X->do_write = xf_rxcall_do_write;
  X->do_close = xf_rxcall_do_close;
  X->is_writable = (flag != O_RDONLY);
  i->writemode = (flag == O_WRONLY);
  X->refcon = i;
  return 0;
}
