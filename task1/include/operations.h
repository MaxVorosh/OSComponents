#ifndef OPERATIONS
#define OPERATIONS

#include <sys/types.h>
#include <sys/stat.h>
#include "inode.h"

int prepare_for_new_inode(const char *path);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_mknod(const char *path, mode_t mode, dev_t dev);
int tmpfs_open(const char *path, struct fuse_file_info *fi);
int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_write(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_getattr(const char *path, struct stat *statbuf);

#endif