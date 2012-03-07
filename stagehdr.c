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

/* stagehdr.c - Parse and dump stage backup headers */

#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dumpscan.h"
#include "dumpscan_errs.h"
#include "xf_errs.h"
#include "stagehdr.h"

static afs_uint32 hdr_checksum(char *buf, int size)
{
  afs_uint32 sum = 0, n = size / sizeof(afs_uint32), *words = (afs_uint32 *)buf;

  while (--n)
    sum += ntohl(*words++);
  return sum;
}


/* Parse a modern stage backup header.
 * If tag is non-NULL, *tag should contain the first byte (already read),
 * and will be filled in with the first byte after the header, if one exists.
 * On success, returns 0 and leaves us positioned after the header
 * On failure, returns an error and position is undefined
 * Iff there is no header, returns DSERR_MAGIC and leaves us
 * positioned where we started.
 */
afs_uint32 ParseStageHdr(XFILE *X, unsigned char *tag, backup_system_header *hdr)
{
    char buf[128], *x; /* must be long enough for any string in header */
    afs_uint32 i32, j32, hdrlen, dumplen, r;
    u_int64 where;

#define notours() do {                     \
      if (X->is_seekable) {                \
        r = xfseek(X, &where);             \
        return r ? r : DSERR_MAGIC;        \
      } else return DSERR_MAGIC;           \
    } while (0)

#define checkr(r) do {                     \
      if (r == ERROR_XFILE_EOF) notours(); \
      else if (r) return r;                \
    } while (0)

#define get32(x) do {                      \
      r = xfread(X, (char *)&i32, 4);      \
      checkr(r);                           \
      (x) = ntohl(i32);                    \
    } while (0)

#define getstr(x,l) do {                   \
      memset(buf, 0, sizeof(buf));         \
      r = xfread(X, buf, (l));             \
      checkr(r);                           \
      buf[(l)-1] = 0;                      \
      (x) = strdup(buf);                   \
      if (!(x)) return ENOMEM;             \
    } while (0)

    if (r = xftell(X, &where)) return r;
    if (hdr) memset(hdr, 0, sizeof(*hdr));

    /* Read the magic number first.  It's possible the caller has already
     * read the first character.  If this is the case, we need to check it
     * and return DSERR_MAGIC without reading any more if it is not 'S'.
     * Once that is done, we read the remaining 3 bytes of magic number.
     * If the caller did not pre-read anything, read the whole 4 bytes. */
    if (tag) {
      if (*tag != 'S') return DSERR_MAGIC;
      buf[0] = *tag;
      r = xfread(X, buf + 1, 3);
      checkr(r);
      memcpy(&i32, buf, 4);
      hdr->magic = ntohl(i32);
    } else {
      get32(hdr->magic);
    }

    /* Now the other constant parts, which are easier */
    get32(hdr->version);
    get32(hdrlen);

    /* Check the magic number */
    if (hdr->magic != DUMPHDR_MAGIC) notours();

    /* Now read the rest of the structure */
    switch (hdr->version) {
      case DUMPHDR_VERS:
        if (hdrlen != DUMPHDR_LEN) notours();

        get32(hdr->flags);  /* Really the system type */
        get32(hdr->volid);

        get32(hdr->from_date);
        get32(hdr->to_date);
        hdr->dump_date = hdr->to_date;
        get32(hdr->level);

        get32(dumplen);
        set64(hdr->dumplen, dumplen);

        getstr(hdr->server, DUMPHDR_MAXSYSNAME);
        hdr->part = strdup("");
        if (!hdr->part) return ENOMEM;
        getstr(hdr->volname, DUMPHDR_MAXVOLNAME);
        getstr(x, 2); free(x);
        break;


      case DUMPHDR_VERS64:
        if (hdrlen != DUMPHDR_LEN64) notours();

        get32(hdr->flags);  /* Really the system type */
        get32(hdr->volid);

        get32(hdr->from_date);
        get32(hdr->to_date);
        hdr->dump_date = hdr->to_date;
        get32(hdr->level);

        get32(j32);
        get32(dumplen);
        mk64(hdr->dumplen, j32, dumplen);

        getstr(hdr->server, DUMPHDR_MAXSYSNAME);
        hdr->part = strdup("");
        if (!hdr->part) return ENOMEM;
        getstr(hdr->volname, DUMPHDR_MAXVOLNAME);
        break;

      default:
        notours();
    }

  if (tag) return ReadByte(X, tag);
  else return 0;
}


/* Parse a stage "version 20" backup header.
 * If tag is non-NULL, *tag should contain the first byte (already read),
 * and will be filled in with the first byte after the header, if one exists.
 * On success, returns 0 and leaves us positioned after the header
 * On failure, returns an error and position is undefined
 * Iff there is no header, returns DSERR_MAGIC and leaves us
 * positioned where we started.
 */
afs_uint32 ParseStageV20Hdr(XFILE *X, unsigned char *tag, backup_system_header *hdr)
{
  char buf[V20_HDRLEN];
  struct v20_header *bckhdr = (struct v20_header *)buf;
  u_int64 where;
  afs_uint32 r;

  if (r = xftell(X, &where)) return r;
  if (hdr) memset(hdr, 0, sizeof(*hdr));
  if (tag) {
    if (*tag != V20_VERSMIN) return DSERR_MAGIC;
    buf[0] = *tag;
    r = xfread(X, buf + 1, V20_HDRLEN - 1);
  } else {
    r = xfread(X, buf, V20_HDRLEN);
  }

  if (r == ERROR_XFILE_EOF) {
    if (X->is_seekable) {
      r = xfseek(X, &where);
      return r ? r : DSERR_MAGIC;
    } else return DSERR_MAGIC;
  } else if (r) return r;

  if (bckhdr->c_vers < V20_VERSMIN
  ||  ntohl(bckhdr->c_magic) != V20_MAGIC
  ||  hdr_checksum(buf, V20_HDRLEN) != V20_CHECKSUM) {
    if (X->is_seekable) {
      r = xfseek(X, &where);
      return r ? r : DSERR_MAGIC;
    } else return DSERR_MAGIC;
  }

  if (hdr) {
    hdr->version   = bckhdr->c_vers;
    hdr->from_date = ntohl(bckhdr->c_fdate);
    hdr->to_date   = ntohl(bckhdr->c_tdate);
    hdr->dump_date = ntohl(bckhdr->c_time);
    hdr->filenum   = ntohl(bckhdr->c_filenum);
    hdr->volid     = ntohl(bckhdr->c_id);
    set64(hdr->dumplen, ntohl(bckhdr->c_length));
    hdr->level     = ntohl(bckhdr->c_level);
    hdr->magic     = ntohl(bckhdr->c_magic);
    hdr->cksum     = ntohl(bckhdr->c_checksum);
    hdr->flags     = ntohl(bckhdr->c_flags);
    hdr->server    = malloc(strlen(bckhdr->c_host) + 1);
    hdr->part      = malloc(strlen(bckhdr->c_disk) + 1);
    hdr->volname   = malloc(strlen(bckhdr->c_name) + 1);

    if (!hdr->server || !hdr->part || !hdr->volname) {
      if (hdr->server)  free(hdr->server);
      if (hdr->part)    free(hdr->part);
      if (hdr->volname) free(hdr->volname);
      return ENOMEM;
    }
    strcpy(hdr->server,  bckhdr->c_host);
    strcpy(hdr->part,    bckhdr->c_disk);
    strcpy(hdr->volname, bckhdr->c_name);
  }

  if (tag) return ReadByte(X, tag);
  else return 0;
}


/* Dump a stage "version 20" backup header */
afs_uint32 DumpStageV20Hdr(XFILE *OX, backup_system_header *hdr)
{
  char buf[V20_HDRLEN];
  struct v20_header *bckhdr = (struct v20_header *)buf;
  afs_uint32 checksum;
  afs_uint32 r;

  memset(buf, 0, V20_HDRLEN);
  bckhdr->c_vers     = hdr->version;
  bckhdr->c_fdate    = htonl(hdr->from_date);
  bckhdr->c_tdate    = htonl(hdr->to_date);
  bckhdr->c_filenum  = htonl(hdr->filenum);
  bckhdr->c_time     = htonl(hdr->dump_date);
  bckhdr->c_id       = htonl(hdr->volid);
  bckhdr->c_length   = htonl(lo64(hdr->dumplen));
  bckhdr->c_level    = htonl(hdr->level);
  bckhdr->c_magic    = htonl(V20_MAGIC);
  bckhdr->c_flags    = htonl(hdr->flags);

  strcpy(bckhdr->c_host, hdr->server);
  strcpy(bckhdr->c_disk, hdr->part);
  strcpy(bckhdr->c_name, hdr->volname);

  /* Now, compute the checksum */
  checksum = hdr_checksum(buf, V20_HDRLEN);
  bckhdr->c_checksum = htonl(V20_CHECKSUM - checksum);

  if (r = xfwrite(OX, buf, V20_HDRLEN)) return r;
  return 0;
}
