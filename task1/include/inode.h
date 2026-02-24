#ifndef INODE
#define INODE

#include "consts.h"

struct inode_stat {
    unsigned int owner_;
    unsigned short umask_;
};

struct dir_data {
    char *name_;
    unsigned int position_;
};

union inode_data {
    char *file_data_;
    struct dir_data dir_data_[MAX_FILES_IN_DIRECTORY];
};

struct inode {
    struct inode_stat stat_;
    union inode_data data_;
    unsigned long size_;
    unsigned int flags_;
    short inner_flags_;
};

#define IS_DIR(inode) (inode.inner_flags_ & IS_DIR_MASK)
#define IS_USED(inode) (inode.inner_flags_ & IS_USED_MASK)

struct inode inodes[MAX_INODES];

int add_start_inode();
int find_subdir(int dir_position, char *name);
int parse_path(char *path, struct dir_data *data, int is_exists);
int find_position();
int get_empty_dir_data(union inode_data *data);

#endif