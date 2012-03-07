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

/* xf_voldump.c - XFILE module for AFS volume dumps */

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

struct vdinfo {
  struct rx_connection *conn;  /* our connection */
  struct rx_call *call;        /* our active call */
  afs_int32 tid;               /* volser transaction ID */
  XFILE rx;                    /* underlying rx xfile */
  int destconn;                /* if set, destroy connection on close */
};


static afs_uint32 xf_voldump_do_read(XFILE *X, void *buf, afs_uint32 count)
{
  struct vdinfo *i = X->refcon;
  return xfread(&(i->rx), buf, count);
}


static afs_uint32 xf_voldump_do_write(XFILE *X, void *buf, afs_uint32 count)
{
  struct vdinfo *i = X->refcon;
  return xfwrite(&(i->rx), buf, count);
}


static afs_uint32 xf_voldump_do_close(XFILE *X)
{
  struct vdinfo *i = X->refcon;
  afs_uint32 code, rcode, xcode;

  code = xfclose(&(i->rx));
  code = rx_EndCall(i->call, code);
  xcode = AFSVolEndTrans(i->conn, i->tid, &rcode);
  if (!code) code = xcode ? xcode : rcode;
  if (i->destconn) rx_DestroyConnection(i->conn);
  free(i);
  return code;
}


afs_uint32 xfopen_voldump(XFILE *X, struct rx_connection *conn,
                          afs_int32 part, afs_int32 volid, afs_int32 date)
{
  struct vdinfo *i;
  afs_uint32 code, rcode;


  memset(X, 0, sizeof(*X));
  if (!(i = (struct vdinfo *)malloc(sizeof(struct vdinfo)))) return ENOMEM;
  memset(i, 0, sizeof(struct vdinfo));
  i->conn = conn;
  code = AFSVolTransCreate(i->conn, volid, part, ITBusy, &(i->tid));
  if (code) {
    free(i);
    return code;
  }
  i->call = rx_NewCall(i->conn);
  if ((code = StartAFSVolDump(i->call, i->tid, date))
  ||  (code = xfopen_rxcall(&(i->rx), O_RDONLY, i->call))) {
    rx_EndCall(i->call, 0);
    AFSVolEndTrans(i->conn, i->tid, &rcode);
    free(i);
    return code;
  }

  X->do_read     = xf_voldump_do_read;
  X->do_write    = xf_voldump_do_write;
  X->do_close    = xf_voldump_do_close;
  X->is_writable = i->rx.is_writable;
  return 0;
}


afs_uint32 xfon_voldump(XFILE *X, int flag, char *name)
{
  struct hostent *he;
  struct rx_securityClass *class;
  struct rx_connection *conn;
  struct ktc_principal sname;
  struct ktc_token token;
  struct afsconf_dir *confdir;
  afs_uint32 code, server_addr;
  afs_int32 volid, partid, date;
  int isnum, index;
  char *x, *y;

  /* Parse out the optional date and server location */
  if (code = rx_Init(0)) return code;
  if (!(name = strdup(name))) return ENOMEM;
  if (x = strrchr(name, ',')) {
    *x++ = 0;
    date = atoi(x);
  } else {
    date = 0;
  }
  if (x = strrchr(name, '@')) {
    int a, b, c, d;

    *x++ = 0;
    if (!(y = strchr(x, '/'))) {
      free(name);
      return VL_BADPARTITION;
    }
    *y++ = 0;
    if (sscanf(x, "%d.%d.%d.%d", &a, &b, &c, &d) == 4
    &&  a >= 0 && a <= 255 && b >= 0 && b <= 255
    &&  c >= 0 && c <= 255 && d >= 0 && d <= 255) {
      server_addr = (a << 24) | (b << 16) | (c << 8) | d;
      server_addr = htonl(server_addr);
    } else {
      he = gethostbyname(x);
      if (!he) {
        free(name);
        return VL_BADSERVER;
      }
      memcpy(&server_addr, he->h_addr, sizeof(server_addr));
    }
    partid = volutil_GetPartitionID(y);
    if (partid < 0) {
      free(name);
      return VL_BADPARTITION;
    }
  }

  /* Get tokens and set up a security object */
  confdir = afsconf_Open(AFSCONF_CLIENTNAME);
  if (!confdir) {
    free(name);
    return AFSCONF_NODB;
  }
  if (code = afsconf_GetLocalCell(confdir, sname.cell, MAXKTCNAMELEN)) {
    free(name);
    return code;
  }
  afsconf_Close(confdir);
  strcpy(sname.name, "afs");
  sname.instance[0] = 0;
  code = ktc_GetToken(&sname, &token, sizeof(token), 0);
  if (code) {
    class = rxnull_NewClientSecurityObject();
    index = 0;
  } else {
    class = rxkad_NewClientSecurityObject(rxkad_clear, &token.sessionKey,
            token.kvno, token.ticketLen, token.ticket);
    index = 2;
  }

  /* Figure out the volume ID, looking it up in the VLDB if neccessary.
   * Also look up the server and partition, if they were not specified.
   */
  for (isnum = 1, y = name; *y; y++)
    if (*y < '0' || *y > '9') isnum = 0;
  if (isnum) {
    volid = atoi(name);
    if (!x) {
      fprintf(stderr, "XXX: need to lookup volume by ID!\n");
      exit(-1);
    }
  } else {
    fprintf(stderr, "XXX: need to lookup volume by name!\n");
    exit(-1);
  }
  free(name);

  /* Establish a connection and start the call */
  conn = rx_NewConnection(server_addr, htons(AFSCONF_VOLUMEPORT),
                          VOLSERVICE_ID, class, index);
  code = xfopen_voldump(X, conn, partid, volid, date);
  if (!code) ((struct vdinfo *)(X->refcon))->destconn = 1;
  return code;
}
