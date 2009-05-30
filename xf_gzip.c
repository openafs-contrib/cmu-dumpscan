/*
 * gzip support for XFILE 
 * Thomas L. Kula <kula@tproa.net>
 * http://kula.tproa.net/
 * 24 January 2009
 *
 * Copyright (C) 2009 Thomas L. Kula
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

/*
 * based heavily on xf_files.c, which is covered by:
 */

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

/* xf_gzip.c - XFILE routines for accessing GZIP files */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <zlib.h>

#include "xfiles.h"
#include "xf_errs.h"

#define O_MODE_MASK (O_RDONLY | O_WRONLY | O_RDWR)


/* do_read for stdio xfiles */
static afs_uint32 xf_GZIP_do_read(XFILE *X, void *buf, afs_uint32 count)
{
  gzFile *G = X->refcon;
  int numread = 0;
  /* XXX: handle short and interrupted reads */
  if ((numread = gzread(G, buf, count)) <=0 )
    return numread == 0 ? ERROR_XFILE_EOF :  numread; ; /* FIX */
  return 0;
}


/* do_write for stdio xfiles */
static afs_uint32 xf_GZIP_do_write(XFILE *X, void *buf, afs_uint32 count)
{
  gzFile *G = X->refcon;

  /* XXX: handle interrupted writes */
  if (gzwrite(G, buf, count) != 1)
    return errno;
  return 0;
}

/*
 * zlib simulates seeking backwards in a gziped file by going
 * back to the start of the file and uncompressing until it
 * has reached the right point. It simulates seeking forward
 * by uncompressing until it has reached the right spot. For 
 * large files, these operations can be painfully slow.
 * 
 * These functions are provided to do seekable operations,
 * but by default seeking is disabled. To turn on/off seekability,
 * twiddle XFILE->is_seekable. This probably needs some thinking.
 */



/* do_tell for stdio xfiles */
static afs_uint32 xf_GZIP_do_tell(XFILE *X, u_int64 *offset)
{
  gzFile *G = X->refcon;
  z_off_t where;

  if (!X->is_seekable) {
    cp64(*offset, X->filepos);
    return 0;
  }
  where = gztell(G);
  if (where == -1) return errno;
  set64(*offset, where);
  return 0;
}


/* do_seek for stdio xfiles */
static afs_uint32 xf_GZIP_do_seek(XFILE *X, u_int64 *offset)
{
  gzFile *G = X->refcon;
  off_t where = get64(*offset);

  if (gzseek(G, where, SEEK_SET) == -1) return errno;
  return 0;
}


/* do_skip for stdio xfiles */
static afs_uint32 xf_GZIP_do_skip(XFILE *X, afs_uint32 count)
{
  gzFile *G = X->refcon;

  if (gzseek(G, (z_off_t)count, SEEK_CUR) == -1) return errno;
  return 0;
}


/* do_close for stdio xfiles */
static afs_uint32 xf_GZIP_do_close(XFILE *X)
{
  gzFile *G = X->refcon;
  int zerr = 0;

  X->refcon = 0;
  if (zerr = gzclose(G)) return zerr;
  return 0;
}


/* Prepare a stdio XFILE */
static void gzprepare(XFILE *X, gzFile *G, int xflag)
{

  memset(X, 0, sizeof(*X));
  X->do_read  = xf_GZIP_do_read;
  X->do_write = xf_GZIP_do_write;
  X->do_tell  = xf_GZIP_do_tell;
  X->do_close = xf_GZIP_do_close;
  X->refcon = G;
  if (xflag == O_RDWR) X->is_writable = 1;
  X->is_seekable = 0;
  X->do_seek = xf_GZIP_do_seek;
  X->do_skip = xf_GZIP_do_skip;

}


/* Open a gzipped XFILE by path */
afs_uint32 xfopen_gzip(XFILE *X, int flag, char *path, int mode)
{
  gzFile *G = 0;
  int fd = -1, xflag;
  afs_uint32 code;

  xflag = flag & O_MODE_MASK;
  if (xflag == O_WRONLY) xflag = O_RDWR;

  if ((fd = open(path, flag, mode)) < 0) return errno;
  if (!(G = gzdopen( fd, (xflag == O_RDONLY) ? "rb" : "wb"))) {
    code = errno;
    close(fd);
    return code;
  }

  gzprepare(X, G, xflag);
  return 0;
}


/* open-by-name support for gziped filenames */
afs_uint32 xfon_gzip(XFILE *X, int flag, char *name)
{
  return xfopen_gzip(X, flag, name, 0644);
}


