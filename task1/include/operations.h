#ifndef OPERATIONS
#define OPERATIONS

#include <sys/types.h>
#include <sys/stat.h>
#include <fuse.h>
#include "inode.h"

int prepare_for_new_inode(const char *path);
int tmpfs_mkdir(const char *path, mode_t mode);
int tmpfs_mknod(const char *path, mode_t mode, dev_t dev);
int tmpfs_open(const char *path, struct fuse_file_info *fi);
int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int tmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi);
int tmpfs_rmdir(const char *path);
int tmpfs_unlink(const char *path);
void tmpfs_destroy(void *userdata);
void destroy_recursive(int inode_position);
int tmpfs_statfs(const char *path, struct statvfs *statv);
int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int tmpfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);
int tmpfs_rename(const char *path, const char *new_path, unsigned int flags);

#endif