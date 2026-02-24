#include <stdio.h>
#include "inode.h"
#include "operations.h"

void tmpfs_usage()
{
    fprintf(stderr, "usage:  tmpfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

struct fuse_operations tmpfs_oper = {
  .getattr = NULL,
  .readlink = NULL,

  .getdir = NULL,
  .mknod = tmpfs_mknod,
  .mkdir = tmpfs_mkdir,
  .unlink = NULL,
  .rmdir = NULL,
  .symlink = NULL,
  .rename = NULL,
  .link = NULL,
  .chmod = NULL,
  .chown = NULL,
  .truncate = NULL,
  .utime = NULL,
  .open = tmpfs_open,
  .read = NULL,
  .write = NULL,

  .statfs = NULL,
  .flush = NULL,
  .release = NULL,
  .fsync = NULL,
  
#ifdef HAVE_SYS_XATTR_H
  .setxattr = NULL,
  .getxattr = NULL,
  .listxattr = NULL,
  .removexattr = NULL,
#endif
  
  .opendir = NULL,
  .readdir = NULL,
  .releasedir = NULL,
  .fsyncdir = NULL,
  .init = NULL,
  .destroy = NULL,
  .access = NULL,
  .ftruncate = NULL,
  .fgetattr = NULL
};

int main(int argc, char *argv[])
{
    int fuse_stat;

    if ((getuid() == 0) || (geteuid() == 0)) {
    	fprintf(stderr, "Running TmpFS as root opens unnacceptable security holes\n");
    	return 1;
    }

    // See which version of fuse we're running
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
    
    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	tmpfs_usage();

    char* rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    int res = add_start_inode();
    if (!res) {
        perror("Cannot allocate memory for start inode");
        abort();
    }
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &tmpfs_oper, rootdir);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
