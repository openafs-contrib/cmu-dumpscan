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

/* genroot.c - Generate a root.afs volume */

#include "dumpscan.h"
#include "dumpfmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <afs/dirpath.h>
#include <afs/acl.h>
#include <afs/prs_fs.h>
#include <afs/com_err.h>


struct rootitem {
  struct rootitem *next;
  afs_uint32 vnode, uniq;
  char *target;
  char kind;
};


static struct rootitem *root_head, *root_tail;
static dir_state *DS;
static afs_uint32 next_vnode;
static afs_uint32 next_uniq;
static afs_uint32 when;

static int debug, doaliases, dorft, doallro;
static const char *argv0, *csdbpath, *aliaspath;
static char *outpath;
static char **rocells;
static int nrocells;

static void usage(int status, char *msg)
{
  if (msg) fprintf(stderr, "%s: %s\n", argv0, msg);
  fprintf(stderr, "Usage: %s [options] [CellServDB [CellAlias]]\n", argv0);
  fprintf(stderr, "  -a          Generate short cell-name aliases\n");
  fprintf(stderr, "  -r cell     Generate RO mount point for cell\n");
  fprintf(stderr, "  -r +        Generate RO mount points for all cells\n");
  fprintf(stderr, "  -t          Generate recursive-find-trap\n\n");
  fprintf(stderr, "  -d          Enable debug output\n");
  fprintf(stderr, "  -h          Print this help message\n");
  fprintf(stderr, "  -o outfile  Put output in file [default stdout]\n");
  fprintf(stderr, "Default CellServDB: %s\n",
          AFSDIR_CLIENT_CELLSERVDB_FILEPATH);
  fprintf(stderr, "Default CellAlias:  %s\n",
          AFSDIR_CLIENT_CELLALIAS_FILEPATH);
  exit(status);
}

static void parse_options(int argc, char **argv)
{
  char c;

  /* Set the program name */
  if (argv0 = strrchr(argv[0], '/')) argv0++;
  else argv0 = argv[0];

  /* Initialize options */
  debug = doaliases = dorft = doallro = nrocells = 0;
  rocells = 0;
  outpath   = 0;
  csdbpath  = AFSDIR_CLIENT_CELLSERVDB_FILEPATH;
  aliaspath = AFSDIR_CLIENT_CELLALIAS_FILEPATH;

  /* Parse the options */
  while ((c = getopt(argc, argv, "ar:thdo:")) != EOF) {
    switch (c) {
      default:  usage(1, "Invalid option!");
      case 'h': usage(0, 0);
      case 'a': doaliases    = 1;                         continue;
      case 't': dorft        = 1;                         continue;
      case 'd': debug        = 1;                         continue;
      case 'o': outpath      = optarg;                    continue;
      case 'r': 
        if (!strcmp(optarg, "+")) doallro = 1;
        else {
          if (nrocells)
            rocells = realloc(rocells, (nrocells + 1) * sizeof(char *));
          else
            rocells = malloc(sizeof(char *));
          if (!rocells) {
            fprintf(stderr, "out of memory!\n");
            exit(1);
          }
          rocells[nrocells] = malloc(strlen(optarg) + 2);
          if (!rocells[nrocells]) {
            fprintf(stderr, "out of memory!\n");
            exit(1);
          }
          rocells[nrocells][0] = '.';
          strcpy(rocells[nrocells] + 1, optarg);
          nrocells++;
        }
    }
  }

  if (argc > optind) csdbpath  = argv[optind++];
  if (argc > optind) aliaspath = argv[optind++];
  if (argc > optind) usage(1, "Too many arguments!");
}

static void die(const char *context, afs_uint32 code)
{
  fprintf(stderr, "%s: %s: %s", argv0, context, afs_error_message(code));
  exit(1);
}


/* Add a mount point or symlink to the root directory.
 * Kind is the kind of mount point, or 0 for a symlink.
 */
static void add_item(char *name, char *cell, char *vol, char kind)
{
  struct rootitem *item;
  afs_uint32 r, l;

  if (debug)
    fprintf(stderr, "add_item %s -> %s:%s (%c)\n",
            name, cell ? cell : "<>", vol, kind ? kind : '-');

  /* Allocate and initialize the item */
  item = malloc(sizeof(*item));
  if (!item) die("add_item", ENOMEM);
  memset(item, 0, sizeof(*item));

  /* Allocate and fill in target string */
  l = strlen(vol) + 1;
  if (cell) l += strlen(cell) + 1;
  if (kind) l+=2;
  item->target = malloc(l);
  if (!item->target) die("add_item", ENOMEM);
  if (kind) item->target[0] = kind;
  if (cell) sprintf(item->target + !!kind, "%s:%s%s",
                    cell, vol, kind ? "." : "");
  else      sprintf(item->target + !!kind, "%s%s", vol, kind ? "." : "");

  /* Fill in the item and add it to the list */
  item->vnode = next_vnode;
  item->uniq  = next_uniq;
  item->kind  = kind;
  if (root_tail) root_tail->next = item;
  else root_head = item;
  root_tail = item;

  /* Now add the directory entry */
  r = Dir_AddEntry(DS, name, next_vnode, next_uniq);
  if (r) die("addentry", r);

  /* Finally increment the counters */
  next_vnode += 2;
  next_uniq++;
}


void parse_csdb(void)
{
  char buf[256], *x, *y;
  FILE *F;

  F = fopen(csdbpath, "r");
  if (!F) die(csdbpath, errno);
  while (fgets(buf, sizeof(buf), F)) {
    if (buf[0] != '>') continue;
    for (x = buf+1; *x && isspace(*x); x++);
    if (!*x) continue;
    for (y = x; *y && !isspace(*y); y++);
    *y = 0;
    add_item(x, x, "root.cell", '#');
    if (doallro) {
      x[-1] = '.';
      add_item(x - 1, x, "root.cell", '%');
    }
  }
  fclose(F);
}

void parse_aliases(void)
{
  char buf[256], *x, *y, *z;
  FILE *F;

  F = fopen(aliaspath, "r");
  if (!F && errno == ENOENT) return;
  if (!F) die(aliaspath, errno);
  while (fgets(buf, sizeof(buf), F)) {
    for (x = buf; *x && isspace(*x); x++);
    if (!*x) continue;

    for (y = x; *y && !isspace(*y); y++);
    while (isspace(*y)) *y++ = 0;
    if (!*y) continue;

    for (z = y; *z && !isspace(*z); z++);
    *z = 0;

    add_item(y, 0, x, 0);
  }
  fclose(F);
}

void emit(void)
{
  struct acl_accessList *acl;
  struct rootitem *item;
  afs_dump_header dumphdr;
  afs_vol_header volhdr;
  afs_vnode vnode;
  afs_uint32 r;
  XFILE X;

  if (outpath) r = xfopen(&X, O_RDWR|O_CREAT|O_TRUNC, outpath);
  else         r = xfopen_FILE(&X, O_RDWR, stdout);
  if (r) die("xfopen", r);

  /* Dump the dump header */
  memset(&dumphdr, 0, sizeof(dumphdr));
  dumphdr.field_mask = F_DUMPHDR_VOLID | F_DUMPHDR_VOLNAME
                     | F_DUMPHDR_FROM  | F_DUMPHDR_TO;
  dumphdr.magic   = DUMPBEGINMAGIC;
  dumphdr.version = DUMPVERSION;
  dumphdr.volid = 1;
  dumphdr.volname = "root.afs";
  dumphdr.from_date = 0;
  dumphdr.to_date = when;
  if ((r = DumpDumpHeader(&X, &dumphdr))) die("dump header", r);

  /* Dump the volume header */
  memset(&volhdr, 0, sizeof(volhdr));
  volhdr.field_mask = F_VOLHDR_VOLID       | F_VOLHDR_VOLVERS
                    | F_VOLHDR_VOLNAME     | F_VOLHDR_INSERV
                    | F_VOLHDR_BLESSED     | F_VOLHDR_VOLUNIQ
                    | F_VOLHDR_VOLTYPE     | F_VOLHDR_PARENT
                    | F_VOLHDR_MAXQ        | F_VOLHDR_DISKUSED
                    | F_VOLHDR_NFILES      | F_VOLHDR_ACCOUNT
                    | F_VOLHDR_OWNER       | F_VOLHDR_CREATE_DATE
                    | F_VOLHDR_ACCESS_DATE | F_VOLHDR_UPDATE_DATE
                    | F_VOLHDR_EXPIRE_DATE | F_VOLHDR_BACKUP_DATE
                    | F_VOLHDR_OFFLINE_MSG | F_VOLHDR_MOTD
                    | F_VOLHDR_WEEKUSE     | F_VOLHDR_DAYUSE
                    | F_VOLHDR_DAYUSE_DATE;
  volhdr.volid = 1;
  volhdr.volvers = 1;
  volhdr.volname = "root.afs";
  volhdr.flag_inservice = 1;
  volhdr.flag_blessed = 1;
  volhdr.voluniq = next_uniq;
  volhdr.voltype = 0;
  volhdr.parent_volid = 1;
  volhdr.nfiles = next_vnode >> 1;
  volhdr.diskused = volhdr.nfiles + 2;
  volhdr.maxquota = volhdr.diskused + 10000;
  volhdr.create_date = when;
  volhdr.update_date = when;
  volhdr.offline_msg = "Generated by genrootafs";
  volhdr.motd_msg = "";
  if ((r = DumpVolumeHeader(&X, &volhdr))) die("vol header", r);

  /* Dump the root directory */
  memset(&vnode, 0, sizeof(vnode));
  vnode.field_mask  = F_VNODE_TYPE   | F_VNODE_NLINKS
                    | F_VNODE_PARENT | F_VNODE_DVERS
                    | F_VNODE_AUTHOR | F_VNODE_OWNER
                    | F_VNODE_GROUP  | F_VNODE_MODE
                    | F_VNODE_CDATE  | F_VNODE_SDATE
                    | F_VNODE_ACL;
  vnode.vnode       = 1;
  vnode.vuniq       = 1;
  vnode.type        = vDirectory;
  vnode.nlinks      = 2;
  vnode.parent      = 0;
  vnode.datavers    = when;
  vnode.mode        = 0755;
  vnode.client_date = when;
  vnode.server_date = when;

#define N_ACL_ENTRIES 2
  acl = (struct acl_accessList *)vnode.acl;
  acl->size     = htonl(sizeof(struct acl_accessList) +
                        (N_ACL_ENTRIES - 1) * sizeof(struct acl_accessEntry));
  acl->version  = htonl(ACL_ACLVERSION);
  acl->total    = htonl(N_ACL_ENTRIES);
  acl->positive = htonl(N_ACL_ENTRIES);
  acl->entries[0].id     = htonl(-204);
  acl->entries[0].rights = htonl(PRSFS_READ   | PRSFS_LOOKUP | PRSFS_INSERT |
                                 PRSFS_DELETE | PRSFS_WRITE  | PRSFS_LOCK   |
                                 PRSFS_ADMINISTER);
  acl->entries[1].id     = htonl(-101);
  acl->entries[1].rights = htonl(PRSFS_READ | PRSFS_LOOKUP);
  if ((r = DumpVNode(&X, &vnode))) die("root info", r);
  if ((r = Dir_EmitData(DS, &X, 1))) die("root contents", r);

  for (item = root_head; item; item = item->next) {
    u_int64 tmp64;

    memset(&vnode, 0, sizeof(vnode));
    vnode.field_mask  = F_VNODE_TYPE   | F_VNODE_NLINKS
                      | F_VNODE_PARENT | F_VNODE_DVERS
                      | F_VNODE_AUTHOR | F_VNODE_OWNER
                      | F_VNODE_GROUP  | F_VNODE_MODE
                      | F_VNODE_CDATE  | F_VNODE_SDATE;
    vnode.vnode       = item->vnode;
    vnode.vuniq       = item->uniq;
    vnode.type        = vSymlink;
    vnode.nlinks      = 1;
    vnode.parent      = 1;
    vnode.datavers    = when;
    vnode.mode        = (item->kind ? 0644 : 0755);
    vnode.client_date = when;
    vnode.server_date = when;
    if ((r = DumpVNode(&X, &vnode))) die("vnode info", r);

    mk64(tmp64, 0, strlen(item->target));
    if ((r = DumpVNodeData(&X, item->target, &tmp64)))
      die("vnode contents", r);
  }
  if ((r = DumpDumpEnd(&X))) die("dump end", r);
  if ((r = xfclose(&X))) die("close", r);
}


int main(int argc, char **argv)
{
  afs_uint32 r;
  int i;

  parse_options(argc, argv);
  initialize_AVds_error_table();
  initialize_xFil_error_table();

  when = time(0);
  root_head = root_tail = 0;
  next_vnode = 2;
  next_uniq  = when;

  /* Generate the root directory and vnode list */
  if ((r = Dir_Init(&DS)) ||
      (r = Dir_AddEntry(DS, ".", 1, 1)) ||
      (r = Dir_AddEntry(DS, "..", 1, 1)))
    die("setup", r);

  if (dorft) add_item(".recursive-find-trap", 0, "root.afs", '#');
  parse_csdb();
  if (!doallro) {
    for (i = 0; i < nrocells; i++)
      add_item(rocells[i], rocells[i]+1, "root.cell", '%');
  }
  if (doaliases) parse_aliases();
  if (r = Dir_Finalize(DS)) die("finalize", r);
  emit();
  return 0; /* success */
}
