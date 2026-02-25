#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "operations.h"
#include "inode.h"
#include "errno.h"

int add_inode(const char *path, struct inode inode) {
    struct dir_data data;
    int res = parse_path(path, &data, 0);
    if (res < 0) {
        return res;
    }
    int position = find_position();
    if (position < 0) {
        return -EDQUOT;
    }
    
    struct inode *parent_inode = &inodes[data.position_];
    if (parent_inode->size_ == MAX_FILES_IN_DIRECTORY) {
        return -EDQUOT;
    }
    struct dir_data new_dir_data = {data.name_, position};
    parent_inode->data_.dir_data_[parent_inode->size_] = new_dir_data;
    parent_inode->size_++;

    inode.parent_ = data.position_;
    inodes[position] = inode;
    return position;
}

int tmpfs_mkdir(const char *path, mode_t mode) {
    fprintf(stderr, "Mkdir %s\n", path);
    union inode_data inode_data;
    get_empty_dir_data(&inode_data);
    struct inode inode = {
        {0, 0, mode},
        inode_data,
        0,
        0,
        IS_DIR_MASK | IS_USED_MASK,
        0
    };
    return add_inode(path, inode);
}

int tmpfs_mknod(const char *path, mode_t mode, dev_t dev) {
    fprintf(stderr, "Mknod %s\n", path);
    union inode_data inode_data;
    inode_data.file_data_ = 0;
    struct inode inode = {
        {0, 0, mode},
        inode_data,
        0,
        0,
        IS_USED_MASK,
        0
    };
    return add_inode(path, inode);
}

// Not creating file
int tmpfs_open(const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "Open %s\n", path);
    struct dir_data data;
    int res = parse_path(path, &data, 0);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    inode->flags_ = fi->flags;
    return 0;
}

int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    fprintf(stderr, "Read %s\n", path);
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    if (IS_DIR((*inode))) {
        return -EISDIR;
    }
    if (inode->flags_ & O_WRONLY) {
        return -EBADF;
    }
    if (offset >= inode->size_) {
        return -ENXIO;
    }
    if (offset < 0) {
        return -EINVAL;
    }
    memcpy(buf, inode->data_.file_data_ + offset, size);
    return 0;
}

int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    fprintf(stderr, "Write %s\n", path);
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    if (IS_DIR((*inode))) {
        return -EISDIR;
    }
    if (inode->flags_ & O_RDONLY) {
        return -EBADF;
    }
    if (offset >= inode->size_) {
        return -ENXIO;
    }
    if (offset < 0) {
        return -EINVAL;
    }
    if (offset + size >= inode->size_) {
        char *new_ptr = realloc(inode->data_.file_data_, offset + size);
        if (new_ptr == 0) {
            return -ENOSPC;
        }
        inode->data_.file_data_ = new_ptr;
        inode->size_ = offset + size;
    }
    memcpy(inode->data_.file_data_ + offset, buf, size);
    return size;
}

int tmpfs_getattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi) {
    fprintf(stderr, "Getattr %s\n", path);
    memset(statbuf, 0, sizeof(struct stat));
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    statbuf->st_size = inode->size_;
    if (IS_DIR((*inode))) {
        statbuf->st_size *= sizeof(struct dir_data);
    }
    statbuf->st_blocks = statbuf->st_size / 512;
    statbuf->st_mode = inode->stat_.umask_;
    statbuf->st_uid =  inode->stat_.owner_;
    statbuf->st_gid = inode->stat_.group_;
    statbuf->st_atime = 0;
    statbuf->st_mtime = 0;
    statbuf->st_ctime = 0;
    statbuf->st_nlink = 2;
    statbuf->st_blksize = 4096;
    fprintf(stderr, "getattr end\n");
    return 0;
}

int tmpfs_rmdir(const char *path) {
    fprintf(stderr, "Rmdir %s\n", path);
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    if (!IS_DIR((*inode))) {
        return -ENOTDIR;
    }
    if (inode->size_ != 0) {
        return -ENOTEMPTY;
    }
    remove_subdir(inode->parent_, data.name_);
    free(inode->data_.dir_data_);
    inode->inner_flags_ ^= IS_USED_MASK;
    return 0;
}

int tmpfs_unlink(const char *path) {
    fprintf(stderr, "Unlink %s\n", path);
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    if (IS_DIR((*inode))) {
        return -EISDIR;
    }
    remove_subdir(inode->parent_, data.name_);
    free(inode->data_.file_data_);
    inode->inner_flags_ ^= IS_USED_MASK;
    return 0;
}

void tmpfs_destroy(void *userdata) {
    destroy_recursive(0);
    free(inodes);
}

void destroy_recursive(int inode_position) {
    struct inode inode = inodes[inode_position];
    if (!IS_DIR(inode)) {
        free(inode.data_.file_data_);
        return;
    }
    for (int i = 0; i < inode.size_; ++i) {
        destroy_recursive(inode.data_.dir_data_[i].position_);
    }
    free(inode.data_.dir_data_);
}

int tmpfs_statfs(const char *path, struct statvfs *statv) {
    fprintf(stderr, "Statfs %s\n", path);
    statv->f_bsize = 4096;
    int free_nodes = 0;
    for (int i = 0; i < MAX_INODES; ++i) {
        if (!IS_USED(inodes[i])) {
            free_nodes++;
        }
    }
    statv->f_files = MAX_INODES;
    statv->f_ffree = free_nodes;
    statv->f_namemax = MAX_PATH;
    return 0;
}

int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    fprintf(stderr, "readdir\n");
    struct dir_data data;
    int res = parse_path(path, &data, 1);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    if (!IS_DIR((*inode))) {
        return -ENOTDIR;
    }
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    for (int i = 0; i < inode->size_; ++i) {
        filler(buf, inode->data_.dir_data_[i].name_, NULL, 0, 0);
    }
    return 0;
}

int tmpfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    fprintf(stderr, "chmod %s", path);
    return 0;
}

int tmpfs_rename(const char *path, const char *new_path, unsigned int flags) {
    fprintf(stderr, "rename %s %s", path, new_path);
    return 0;
}