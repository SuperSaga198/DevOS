#ifndef DEVOS_FS_H
#define DEVOS_FS_H

#include <stddef.h>

#define FS_SNAPSHOT_RECORD_SIZE 2087U
#define FS_SNAPSHOT_SIZE 133568U

enum fs_node_type {
    FS_NODE_FILE,
    FS_NODE_DIRECTORY,
};

struct fs_node;

void fs_init(void);
struct fs_node *fs_root(void);
struct fs_node *fs_lookup(const char *path);
struct fs_node *fs_child(const struct fs_node *directory, size_t index);
const char *fs_name(const struct fs_node *node);
enum fs_node_type fs_type(const struct fs_node *node);
size_t fs_size(const struct fs_node *node);
int fs_mkdir(const char *path);
int fs_create(const char *path);
int fs_write(const char *path, const void *data, size_t size);
int fs_read(const char *path, void *buffer, size_t capacity, size_t *size);
int fs_remove(const char *path);
int fs_snapshot_export(void *buffer, size_t capacity);
int fs_snapshot_import(const void *buffer, size_t size);

#endif
