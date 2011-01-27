#include <sys/fcntl.h>
#include <stdarg.h>
#include <afs/stds.h>
#include <afs/acl.h>
#include <afs/prs_fs.h>
#include <afs/com_err.h>
#include "xfiles.h"
#include "dumpscan.h"

static char *progname;
static int acl_mask;

#ifndef debug
#define debug 0
#endif


static afs_uint32 dumphdr_cb(afs_dump_header *hdr, XFILE *Xin, void *refcon)
{
  XFILE *Xout = (XFILE *)refcon;
  afs_uint32 r;

  if (debug) fprintf(stderr, "** Dump header\n");
  r = DumpDumpHeader(Xout, hdr);
  if (r && debug) fprintf(stderr, "   error %d\n", r);
  return r;
}


static afs_uint32 volhdr_cb(afs_vol_header *hdr, XFILE *Xin, void *refcon)
{
  XFILE *Xout = (XFILE *)refcon;
  afs_uint32 r;

  if (debug) fprintf(stderr, "** Volume header\n");
  r = DumpVolumeHeader(Xout, hdr);
  if (r && debug) fprintf(stderr, "   error %d\n", r);
  return r;
}


static afs_uint32 vnode_cb(afs_vnode *v, XFILE *Xin, void *refcon)
{
  struct acl_accessList *acl;
  XFILE *Xout = (XFILE *)refcon;
  afs_uint32 r;
  int i, n;

  /* Fix the ACL, if one is present */
  if (v->field_mask & F_VNODE_ACL) {
    acl = (struct acl_accessList *)(v->acl);
    n = ntohl(acl->positive);
    for (i = 0; i < n; i++) {
      acl->entries[i].rights &= acl_mask;
    }
  }

  /* Dump the vnode metadata */
  if (debug) fprintf(stderr, "** Vnode %d.%d\n", v->vnode, v->vuniq);
  r = DumpVNode(Xout, v);
  if (r && debug) fprintf(stderr, "   error %d dumping vnode\n", r);
  if (r) return r;

  /* And the symlink data, if appropriate */
  if (v->field_mask & F_VNODE_LINK_TARGET) {
    if (debug) fprintf(stderr, "   writing symlink target '%s' (%d bytes)\n",
                       v->link_target, v->size);
    r = DumpVNodeData(Xout, v->link_target, v->size);
    if (r && debug) fprintf(stderr, "   error %d writing link target\n", r);
    if (r) return r;
  }

  return 0;
}


static afs_uint32 data_cb(afs_vnode *v, XFILE *Xin, void *refcon)
{
  XFILE *Xout = (XFILE *)refcon;
  afs_uint32 r;

  /* First, handle the vnode metadata */
  r = vnode_cb(v, Xin, refcon);
  if (r) return r;

  if (v->field_mask & F_VNODE_SIZE) {
    if (debug) fprintf(stderr, "   copying %d bytes of data\n", v->size);
    r = CopyVNodeData(Xout, Xin, v->size);
    if (r && debug) fprintf(stderr, "   error %d copying vnode data\n", r);
    if (r) return r;
  } else {
    if (debug) fprintf(stderr, "   no data for vnode\n", v->size);
  }

  v->field_mask = F_VNODE_PARTIAL; /* don't re-dump fields we already have */
  return 0;
}


static afs_uint32 error_cb(afs_uint32 err, int fatal, void *refcon,
                           char *format, ...)
{
  va_list alist;
  va_start(alist, format);
  afs_com_err_va(progname, err, format, alist);
  va_end(alist);
}


int main(int argc, char **argv)
{
  XFILE Xin, Xout;
  dump_parser dp;
  afs_uint32 r;

  progname = argv[0];
  acl_mask = ntohl(PRSFS_READ | PRSFS_LOOKUP);

  initialize_xFil_error_table();
  initialize_AVds_error_table();
  if (argc > 1) {
    r = xfopen_path(&Xin, O_RDONLY, argv[1], 0);
  } else {
    r = xfopen_FILE(&Xin, O_RDONLY, stdin);
  }
  if (r) {
    afs_com_err(progname, r, "opening %s", (argc > 1) ? argv[1] : "<stdin>");
    exit(1);
  }

  memset(&dp, 0, sizeof(dp));
  dp.refcon         = &Xout;
  dp.cb_dumphdr     = dumphdr_cb;
  dp.cb_volhdr      = volhdr_cb;
  dp.cb_vnode_dir   = vnode_cb;
  dp.cb_vnode_file  = vnode_cb;
  dp.cb_vnode_link  = vnode_cb;
  dp.cb_vnode_empty = vnode_cb;
  dp.cb_vnode_wierd = vnode_cb;
  dp.cb_file_data   = data_cb;
  dp.cb_dir_data    = data_cb;
  dp.cb_error       = error_cb;
  if (Xin.is_seekable) dp.flags |= DSFLAG_SEEK;

  r = xfopen_FILE(&Xout, O_WRONLY, stdout);
  if (r) {
    afs_com_err(progname, r, "opening stdout");
    exit(1);
  }

  r = ParseDumpFile(&Xin, &dp);
  xfclose(&Xin);
  if (!r) r = DumpDumpEnd(&Xout);
  if (!r) r = xfclose(&Xout);
  else        xfclose(&Xout);
  if (r) fprintf(stderr, "*** FAILED: %s\n", afs_error_message(r));
  exit(!!r);
}
