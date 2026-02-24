#include "inode.h"

int add_start_inode() {
    union inode_data data;
    get_empty_dir_data(&data);
    struct inode inode = {
        {0, 0, 0},
        data,
        0,
        0,
        IS_USED_MASK | IS_DIR_MASK
    };
    inodes[0] = inode;
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
    return 
}