#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include "inode.h"
#include "errno.h"

struct inode *inodes = NULL;

int add_start_inode() {
    inodes = malloc(MAX_INODES * sizeof(struct inode));
    if (inodes == NULL) {
        return -ENOSPC;
    }
    union inode_data data;
    int res = get_empty_dir_data(&data);
    if (res < 0) {
        return res;
    }
    struct inode inode = {
        {0, 0, S_IFDIR | 0755},
        data,
        0,
        0,
        IS_USED_MASK | IS_DIR_MASK,
        0
    };
    inodes[0] = inode;
    return 0;
}

int find_subdir(int dir_position, char *name) {
    struct inode *dir_inode = &inodes[dir_position];
    for (int i = 0; i < dir_inode->size_; ++i) {
        if (strcmp(dir_inode->data_.dir_data_[i].name_, name) == 0) {
            return dir_inode->data_.dir_data_[i].position_;
        }
    }
    return -1;
}

void remove_subdir(int dir_position, char *name) {
    struct inode *parent_inode = &inodes[dir_position];
    for (int i = 0; i < parent_inode->size_; ++i) {
        if (strcmp(parent_inode->data_.dir_data_[i].name_, name) == 0) {
            parent_inode->data_.dir_data_[i] = parent_inode->data_.dir_data_[parent_inode->size_ - 1];
            parent_inode->size_--;
            break;
        }
    }
}

int parse_path(const char *path, struct dir_data *data, int is_exists) {
    char stack[MAX_DIR_RECURSION][MAX_PATH];
    strcpy(stack[0], "/");
    int depth = 0;
    int l = 0;
    int r = 0;
    int inode_position = 0;
    data->position_ = 0;
    while (1) {
        if (path[r] == '/' || path[r] == 0) {
            if (l != r) {
                if (r - l > MAX_PATH) {
                    return -ENAMETOOLONG;
                }
                if (r - l == 1 && path[l] == '.') {}
                else if (r - l == 2 && path[l] == '.' && path[l + 1] == '.') {
                    if (depth == 0) {
                        return -EACCES;
                    }
                    depth--;
                }
                else if (depth == MAX_DIR_RECURSION - 1) {
                    return -ELOOP;
                }
                else {
                    memcpy(stack[depth], path + l, r - l);
                    stack[depth][r - l] = 0;
                    if (!IS_DIR(inodes[inode_position])) {
                        return -ENOTDIR;
                    }

                    int new_position = find_subdir(inode_position, stack[depth]);
                    if (path[r] == 0 && !is_exists) {
                        fprintf(stderr, "parse path %s", stack[depth]);
                        if (new_position < 0) {
                            strcpy(data->name_, stack[depth]);
                            data->position_ = inode_position;
                            return 0;
                        }
                        else {
                            return -EEXIST;
                        }
                    }
                    else if (new_position < 0) {
                        return -ENOENT;
                    }
                    inode_position = new_position;
                    depth++;
                }
            }
            if (path[r] == 0) {
                strcpy(data->name_, stack[depth]);
                data->position_ = inode_position;
                break;
            }
            l = r + 1;
        }
        r++;
    }
    return 0;
}

int find_position() {
    for (int i = 0; i < MAX_INODES; ++i) {
        if (!IS_USED(inodes[i])) {
            return i;
        }
    }
    return -1;
}

int get_empty_dir_data(union inode_data *data) {
    struct dir_data *arr = malloc(MAX_FILES_IN_DIRECTORY * sizeof(struct dir_data));
    if (!arr) {
        return -1;
    }
    data->dir_data_ = arr;
    return 0;
}
