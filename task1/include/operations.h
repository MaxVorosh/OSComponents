#ifndef OPERATIONS
#define OPERATIONS

#include <sys/types.h>
#include "inode.h"

int prepare_for_new_inode(const char *path);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_mknod(const char *path, mode_t mode, dev_t dev);
int tmpfs_open(const char *path, struct fuse_file_info *fi);

#endif