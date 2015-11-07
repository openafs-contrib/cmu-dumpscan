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

/* afsdump_mtpt.c - Mount point alteration */

#include <sys/fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <afs/stds.h>
#include <afs/com_err.h>
#include <rx/rxkad.h>
#include <ubik.h>
#include <afs/cellconfig.h>
#include <afs/volser.h>
#include <afs/vlserver.h>

#include "dumpscan.h"
#include "dumpscan_errs.h"

#ifndef debug
#define debug 0
#endif

extern int optind;
extern char *optarg;

extern afs_uint32 repair_dumphdr_cb(afs_dump_header *, XFILE *, void *);
extern afs_uint32 repair_volhdr_cb(afs_vol_header *, XFILE *, void *);
extern afs_uint32 repair_vnode_cb(afs_vnode *, XFILE *, void *);

char *argv0;
static char *input_path, *gendump_path;
static int quiet, verbose, error_count;

static path_hashinfo phi;
static dump_parser dp;

static char mtpt_src[128];
static char mtpt_dst[128];
static size_t mtpt_src_size;
static size_t mtpt_dst_size;

/* Print a usage message and exit */
static void usage(int status, char *msg)
{
  if (msg) fprintf(stderr, "%s: %s\n", argv0, msg);
  fprintf(stderr, "Usage: %s [options] src_cell dst_cell\n", argv0);
  fprintf(stderr, "  -h     Print this help message\n");
  fprintf(stderr, "  -q     Quiet mode (don't print errors)\n");
  fprintf(stderr, "  -v     Verbose mode\n");
  exit(status);
}


/* Parse the command-line options */
static void parse_options(int argc, char **argv)
{
  int c;
  char *src_cell, *dst_cell;

  /* Set the program name */
  if (argv0 = strrchr(argv[0], '/')) argv0++;
  else argv0 = argv[0];

  /* Initialize options */
  input_path = gendump_path = "-";
  quiet = verbose = 0;

  /* Initialize other stuff */
  error_count = 0;

  /* Parse the options */
  while ((c = getopt(argc, argv, "hqv")) != EOF) {
    switch (c) {
      case 'q': quiet        = 1;      continue;
      case 'v': verbose      = 1;      continue;
      case 'h': usage(0, 0);
      default:  usage(1, "Invalid option!");
    }
  }

  if (quiet && verbose) usage(1, "Can't specify both -q and -v");

  /* Parse non-option arguments */
  if (argc - optind < 2) usage(1, "Too few arguments!");
  if (argc - optind > 2) usage(1, "Too many arguments!");
  src_cell = argv[optind];
  dst_cell = argv[optind+1];

  if (strlen(src_cell) + 3 > sizeof(mtpt_src)) {
    fprintf(stderr, "source cell %s is too long\n");
    exit(1);
  }
  if (strlen(dst_cell) + 3 > sizeof(mtpt_dst)) {
    fprintf(stderr, "destination cell %s is too long\n");
    exit(1);
  }

  sprintf(mtpt_src, "#%s:", src_cell);
  sprintf(mtpt_dst, "#%s:", dst_cell);
  mtpt_src_size = strlen(mtpt_src);
  mtpt_dst_size = strlen(mtpt_dst);
}

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

static afs_uint32
filter_vnode(afs_vnode *v, void *refcon)
{
  int mode_mtpt = 0;
  char target_mtpt = 0;
  size_t new_size;
  char *new_target;
  size_t link_target_size;

  if (v->type != vSymlink || !(v->field_mask & F_VNODE_LINK_TARGET)) {
    return 0;
  }

  if (hi64(v->size) != 0) {
    /* Symlinks longer than 2^32 are considered bogus to avoid overflow errors. */
    if (!quiet) {
      fprintf(stderr, "%s: vnode %u.%u symlink size exceeds 32 bits.\n",
                      argv0, (unsigned)v->vnode, (unsigned)v->vuniq);
    }
    return DSERR_BOGUS;
  }

  link_target_size = lo64(v->size);
  if (link_target_size == 0) {
    if (!quiet) {
      fprintf(stderr, "warning: vnode %u.%u symlink size is zero, not altering.\n",
                      argv0, (unsigned)v->vnode, (unsigned)v->vuniq);
    }
    return 0;
  }

  if (v->link_target[0] == '#') {
    target_mtpt = '#';
  }
  if (v->link_target[0] == '%') {
    target_mtpt = '%';
  }

  if (target_mtpt && v->link_target[link_target_size-1] != '.') {
    fprintf(stderr, "warning: vnode %u.%u looks like a weird mountpoint"
                    " (symlink target %s)\n",
                    (unsigned)v->vnode, (unsigned)v->vuniq, v->link_target);
  }
  if (v->mode == 0644) {
    mode_mtpt = 1;
  }
  if ((target_mtpt && !mode_mtpt) || (!target_mtpt && mode_mtpt)) {
    fprintf(stderr, "warning: vnode %u.%u mountpoint target/mode mismatch"
                    " (mode 0%o, symlink target %s), assuming %smountpoint\n",
                    (unsigned)v->vnode, (unsigned)v->vuniq, (unsigned)v->mode,
                    v->link_target, (target_mtpt ? "" : "not "));
  }

  if (!target_mtpt) {
    if (debug) fprintf(stderr, "** non-mtpt symlink %u.%u target %s\n",
                               (unsigned)v->vnode, (unsigned)v->vuniq,
                               v->link_target);
    return 0;
  }
  if (link_target_size <= mtpt_src_size) {
    if (debug) fprintf(stderr, "** mtpt symlink too short, not altering, %u.%u"
                               " target %s size %u\n", (unsigned)v->vnode,
                               (unsigned)v->vuniq, v->link_target,
                               link_target_size);
    return 0;
  }
  if (strncmp(&v->link_target[1], &mtpt_src[1], mtpt_src_size-1) != 0) {
    if (debug) fprintf(stderr, "** mtpt symlink does not match filter src, %u.%u"
                               " target %s\n", (unsigned)v->vnode,
                               (unsigned)v->vuniq, v->link_target);
    return 0;
  }
  new_size = link_target_size + mtpt_dst_size - mtpt_src_size;
  new_target = malloc(new_size+1);
  if (!new_target) return ENOMEM;

  sprintf(new_target, "%s%s", mtpt_dst, &v->link_target[mtpt_src_size]);
  new_target[0] = target_mtpt;
  if (debug) fprintf(stderr, "** rewrote mtpt %u.%u from %s (size %u) to "
                             "%s (size %u)\n",
                             (unsigned)v->vnode, (unsigned)v->vuniq,
                             v->link_target, link_target_size,
                             new_target, new_size);
  free(v->link_target);
  v->link_target = new_target;
  set64(v->size, new_size);

  return 0;
}

static afs_uint32 vnode_cb(afs_vnode *v, XFILE *Xin, void *refcon)
{
  struct acl_accessList *acl;
  XFILE *Xout = (XFILE *)refcon;
  afs_uint32 r;
  int i, n;

  /* Dump the vnode metadata */
  if (debug) fprintf(stderr, "** Vnode %d.%d size %u:%u field_mask %x\n", v->vnode, v->vuniq,
                     hi64(v->size), lo64(v->size), (unsigned)v->field_mask);

  r = filter_vnode(v, refcon);
  if (r) return r;

  r = DumpVNode(Xout, v);
  if (r && debug) fprintf(stderr, "   error %d dumping vnode\n", r);
  if (r) return r;

  /* And the symlink data, if appropriate */
  if (v->field_mask & F_VNODE_LINK_TARGET) {
    if (debug) fprintf(stderr, "   writing symlink target '%s' (%u:%u bytes)\n",
                       v->link_target, hi64(v->size), lo64(v->size));
    r = DumpVNodeData(Xout, v->link_target, &v->size);
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

  if (debug) fprintf(stderr, "** Vnode %d.%d size %u:%u field_mask %x\n", (int)v->vnode, (int)v->vuniq,
                     hi64(v->size), lo64(v->size), (unsigned)v->field_mask);

  if (v->field_mask & F_VNODE_SIZE) {
    if (debug) fprintf(stderr, "   copying %u:%u bytes of data\n", hi64(v->size), lo64(v->size));
    r = CopyVNodeData(Xout, Xin, &v->size);
    if (r && debug) fprintf(stderr, "   error %d copying vnode data\n", r);
    if (r) return r;
  } else {
    if (debug) fprintf(stderr, "   no data for vnode\n");
  }

  v->field_mask = F_VNODE_PARTIAL; /* don't re-dump fields we already have */
  return 0;
}


static afs_uint32 error_cb(afs_uint32 err, int fatal, void *refcon,
                           char *format, ...)
{
  va_list alist;
  if (!quiet) {
    va_start(alist, format);
    afs_com_err_va(argv0, err, format, alist);
    va_end(alist);
  }
}

static int RV = 1;

/* Setup for generating a repaired dump */
static afs_uint32 setup_output(XFILE *output_file)
{
  afs_uint32 r;

  r = xfopen(output_file, O_RDWR|O_CREAT|O_TRUNC, gendump_path);
  if (r) return r;

  dp.refcon = output_file;
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
  return 0;
}


/* Main program */
int main(int argc, char **argv)
{
  XFILE input_file, output_file;
  afs_uint32 r;
  int code = 0;

  parse_options(argc, argv);
  initialize_acfg_error_table();
  initialize_AVds_error_table();
  initialize_rxk_error_table();
  initialize_u_error_table();
  initialize_vl_error_table();
  initialize_vols_error_table();
  initialize_xFil_error_table();
  r = xfopen(&input_file, O_RDONLY, input_path);
  if (r) {
    afs_com_err(argv0, r, "opening %s", input_path);
    exit(2);
  }

  memset(&dp, 0, sizeof(dp));
  dp.repair_flags = 0;
  if (input_file.is_seekable) dp.flags |= DSFLAG_SEEK;

  if (gendump_path && (r = setup_output(&output_file))) {
    afs_com_err(argv0, r, "setting up output");
    xfclose(&input_file);
    exit(2);
  }

  dp.print_flags = 0;
  r = ParseDumpFile(&input_file, &dp);
  xfclose(&input_file);
  if (gendump_path) {
    if (!r) r = DumpDumpEnd(&output_file);
    if (!r) r = xfclose(&output_file);
    else xfclose(&output_file);
  }

  if (verbose && error_count) fprintf(stderr, "*** %d errors\n", error_count);
  if (r && !quiet) fprintf(stderr, "*** FAILED: %s\n", afs_error_message(r));
  if (r) {
      code = 3;
  }
  return code;
}
