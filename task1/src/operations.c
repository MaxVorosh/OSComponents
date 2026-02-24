#include <fcntl.h>
#include "operations.h"
#include "inode.h"
#include "errno.h"

int add_inode(const char *path, struct inode inode) {
    struct dir_data data;
    int res = parse_path_new_file(path, &data);
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

    inodes[position] = inode;
    return position;
}

int tmpfs_mkdir(const char *path, mode_t mode) {
    union inode_data inode_data;
    get_empty_dir_data(&inode_data);
    struct inode inode = {
        {0, 0, mode},
        inode_data,
        0,
        0,
        IS_DIR_MASK | IS_USED_MASK
    };
    return add_inode(path, inode);
}

int tmpfs_mknod(const char *path, mode_t mode, dev_t dev) {
    union inode_data inode_data;
    inode_data.file_data_ = 0;
    struct inode inode = {
        {0, 0, mode},
        inode_data,
        0,
        0,
        IS_USED_MASK
    };
    return add_inode(path, inode);
}

// Not creating file
int tmpfs_open(const char *path, struct fuse_file_info *fi) {
    struct dir_data data;
    int res = parse_path_new_file(path, &data);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    inode->flags_ = fi->flags;
    return 0;
}

int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct dir_data data;
    int res = parse_path(path, &data);
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
    if (size + offset >= inode->size_ || offset < 0) {
        return -ENXIO;
    }
    memcpy(buf, inode->data_.file_data_ + offset, size);
    return 0;
}

int tmpfs_write(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct dir_data data;
    int res = parse_path(path, &data);
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
    if (size + offset >= inode->size_ || offset < 0) {
        return -ENXIO;
    }
    memcpy(inode->data_.file_data_ + offset, buf, size);
    return 0;
}

int bb_getattr(const char *path, struct stat *statbuf) {
    struct dir_data data;
    int res = parse_path(path, &data);
    if (res < 0) {
        return res;
    }
    struct inode *inode = &inodes[data.position_];
    statbuf->st_size = inode->size_;
    statbuf->st_mode = inode->stat_.umask_;
    statbuf->st_uid =  inode->stat_.owner_;
    return 0;
}