#include "fs.h"

#include <stdint.h>

#define FS_NODE_CAPACITY 64
#define FS_NAME_CAPACITY 32
#define FS_FILE_CAPACITY 2048
#define FS_SNAPSHOT_USED_OFFSET 0
#define FS_SNAPSHOT_TYPE_OFFSET 1
#define FS_SNAPSHOT_PARENT_OFFSET 2
#define FS_SNAPSHOT_NAME_LENGTH_OFFSET 4
#define FS_SNAPSHOT_SIZE_OFFSET 5
#define FS_SNAPSHOT_NAME_OFFSET 7
#define FS_SNAPSHOT_CONTENTS_OFFSET 39

_Static_assert(FS_NODE_CAPACITY * FS_SNAPSHOT_RECORD_SIZE ==
               FS_SNAPSHOT_SIZE, "filesystem snapshot size mismatch");

struct fs_node {
    char name[FS_NAME_CAPACITY];
    enum fs_node_type type;
    struct fs_node *parent;
    struct fs_node *first_child;
    struct fs_node *next_sibling;
    size_t size;
    uint8_t contents[FS_FILE_CAPACITY];
    uint8_t used;
};

static struct fs_node nodes[FS_NODE_CAPACITY];
static struct fs_node *root;

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static struct fs_node *allocate_node(const char *name,
                                     enum fs_node_type type)
{
    const size_t length = string_length(name);
    if (length == 0 || length >= FS_NAME_CAPACITY) {
        return NULL;
    }

    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        if (nodes[index].used) {
            continue;
        }

        struct fs_node *node = &nodes[index];
        node->used = 1;
        node->type = type;
        node->parent = NULL;
        node->first_child = NULL;
        node->next_sibling = NULL;
        node->size = 0;
        for (size_t character = 0; character <= length; ++character) {
            node->name[character] = name[character];
        }
        return node;
    }
    return NULL;
}

void fs_init(void)
{
    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        nodes[index].used = 0;
    }
    root = allocate_node("/", FS_NODE_DIRECTORY);
    if (root != NULL) {
        root->parent = root;
    }
}

struct fs_node *fs_root(void)
{
    return root;
}

const char *fs_name(const struct fs_node *node)
{
    return node != NULL && node->used ? node->name : NULL;
}

enum fs_node_type fs_type(const struct fs_node *node)
{
    return node != NULL && node->used ? node->type : FS_NODE_FILE;
}

size_t fs_size(const struct fs_node *node)
{
    return node != NULL && node->used ? node->size : 0;
}

struct fs_node *fs_child(const struct fs_node *directory, size_t index)
{
    if (directory == NULL || !directory->used ||
        directory->type != FS_NODE_DIRECTORY) {
        return NULL;
    }

    struct fs_node *child = directory->first_child;
    while (child != NULL && index != 0) {
        child = child->next_sibling;
        --index;
    }
    return child;
}

static struct fs_node *find_child(const struct fs_node *directory,
                                  const char *name, size_t length)
{
    for (struct fs_node *child = directory->first_child; child != NULL;
         child = child->next_sibling) {
        size_t index = 0;
        while (index < length && child->name[index] == name[index]) {
            ++index;
        }
        if (index == length && child->name[index] == '\0') {
            return child;
        }
    }
    return NULL;
}

struct fs_node *fs_lookup(const char *path)
{
    if (root == NULL || path == NULL || path[0] != '/') {
        return NULL;
    }

    struct fs_node *current = root;
    size_t position = 1;
    while (path[position] != '\0') {
        while (path[position] == '/') {
            ++position;
        }
        if (path[position] == '\0') {
            break;
        }

        const size_t start = position;
        while (path[position] != '\0' && path[position] != '/') {
            ++position;
        }
        const size_t length = position - start;

        if (length == 1 && path[start] == '.') {
            continue;
        }
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            current = current->parent;
            continue;
        }
        if (current->type != FS_NODE_DIRECTORY) {
            return NULL;
        }
        current = find_child(current, path + start, length);
        if (current == NULL) {
            return NULL;
        }
    }
    return current;
}

static int split_parent(const char *path, struct fs_node **parent,
                        char name[FS_NAME_CAPACITY])
{
    if (path == NULL || path[0] != '/') {
        return 0;
    }

    size_t length = string_length(path);
    while (length > 1 && path[length - 1] == '/') {
        --length;
    }
    if (length <= 1) {
        return 0;
    }

    size_t start = length;
    while (start > 0 && path[start - 1] != '/') {
        --start;
    }
    const size_t name_length = length - start;
    if (name_length == 0 || name_length >= FS_NAME_CAPACITY ||
        (name_length == 1 && path[start] == '.') ||
        (name_length == 2 && path[start] == '.' && path[start + 1] == '.')) {
        return 0;
    }

    for (size_t index = 0; index < name_length; ++index) {
        name[index] = path[start + index];
    }
    name[name_length] = '\0';

    if (start == 1) {
        *parent = root;
    } else {
        size_t parent_length = start - 1;
        char parent_path[FS_NAME_CAPACITY * 4];
        if (parent_length >= sizeof(parent_path)) {
            return 0;
        }
        for (size_t index = 0; index < parent_length; ++index) {
            parent_path[index] = path[index];
        }
        parent_path[parent_length] = '\0';
        *parent = fs_lookup(parent_path);
    }
    return *parent != NULL && (*parent)->type == FS_NODE_DIRECTORY;
}

static int add_entry(const char *path, enum fs_node_type type)
{
    struct fs_node *parent;
    char name[FS_NAME_CAPACITY];
    if (!split_parent(path, &parent, name) ||
        find_child(parent, name, string_length(name)) != NULL) {
        return 0;
    }

    struct fs_node *node = allocate_node(name, type);
    if (node == NULL) {
        return 0;
    }
    node->parent = parent;

    struct fs_node **link = &parent->first_child;
    while (*link != NULL) {
        link = &(*link)->next_sibling;
    }
    *link = node;
    return 1;
}

int fs_mkdir(const char *path)
{
    return add_entry(path, FS_NODE_DIRECTORY);
}

int fs_create(const char *path)
{
    return add_entry(path, FS_NODE_FILE);
}

int fs_write(const char *path, const void *data, size_t size)
{
    struct fs_node *node = fs_lookup(path);
    if (node == NULL || node->type != FS_NODE_FILE ||
        size > sizeof(node->contents) || (size != 0 && data == NULL)) {
        return 0;
    }

    const uint8_t *source = data;
    for (size_t index = 0; index < size; ++index) {
        node->contents[index] = source[index];
    }
    node->size = size;
    return 1;
}

int fs_read(const char *path, void *buffer, size_t capacity, size_t *size)
{
    struct fs_node *node = fs_lookup(path);
    if (node == NULL || node->type != FS_NODE_FILE || size == NULL ||
        node->size > capacity || (node->size != 0 && buffer == NULL)) {
        return 0;
    }

    uint8_t *destination = buffer;
    for (size_t index = 0; index < node->size; ++index) {
        destination[index] = node->contents[index];
    }
    *size = node->size;
    return 1;
}

static void release_subtree(struct fs_node *node)
{
    struct fs_node *child = node->first_child;
    while (child != NULL) {
        struct fs_node *next = child->next_sibling;
        release_subtree(child);
        child = next;
    }
    node->used = 0;
}

int fs_remove(const char *path)
{
    struct fs_node *node = fs_lookup(path);
    if (node == NULL || node == root) {
        return 0;
    }

    if (node->type == FS_NODE_DIRECTORY && node->first_child != NULL) {
        return 0;
    }

    struct fs_node **link = &node->parent->first_child;
    while (*link != NULL && *link != node) {
        link = &(*link)->next_sibling;
    }
    if (*link != node) {
        return 0;
    }
    *link = node->next_sibling;
    release_subtree(node);
    return 1;
}

static uint16_t snapshot_read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static void snapshot_write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

int fs_snapshot_export(void *buffer, size_t capacity)
{
    if (root != &nodes[0] || buffer == NULL ||
        capacity < FS_SNAPSHOT_SIZE) {
        return 0;
    }

    uint8_t *bytes = buffer;
    for (size_t index = 0; index < FS_SNAPSHOT_SIZE; ++index) {
        bytes[index] = 0;
    }

    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        const struct fs_node *node = &nodes[index];
        if (!node->used) {
            continue;
        }
        if (node->type != FS_NODE_FILE &&
            node->type != FS_NODE_DIRECTORY) {
            return 0;
        }
        const size_t name_length = string_length(node->name);
        if (name_length == 0 || name_length >= FS_NAME_CAPACITY ||
            node->size > FS_FILE_CAPACITY ||
            (node->type == FS_NODE_DIRECTORY && node->size != 0)) {
            return 0;
        }

        size_t parent_index = 0;
        if (node != root) {
            if (node->parent < &nodes[0] ||
                node->parent >= &nodes[FS_NODE_CAPACITY] ||
                !node->parent->used ||
                node->parent->type != FS_NODE_DIRECTORY) {
                return 0;
            }
            parent_index = (size_t)(node->parent - &nodes[0]);
        }

        uint8_t *record = bytes + index * FS_SNAPSHOT_RECORD_SIZE;
        record[FS_SNAPSHOT_USED_OFFSET] = 1;
        record[FS_SNAPSHOT_TYPE_OFFSET] = (uint8_t)node->type;
        snapshot_write_u16(record + FS_SNAPSHOT_PARENT_OFFSET,
                           (uint16_t)parent_index);
        record[FS_SNAPSHOT_NAME_LENGTH_OFFSET] = (uint8_t)name_length;
        snapshot_write_u16(record + FS_SNAPSHOT_SIZE_OFFSET,
                           (uint16_t)node->size);
        for (size_t character = 0; character < name_length; ++character) {
            record[FS_SNAPSHOT_NAME_OFFSET + character] =
                (uint8_t)node->name[character];
        }
        for (size_t byte = 0; byte < node->size; ++byte) {
            record[FS_SNAPSHOT_CONTENTS_OFFSET + byte] = node->contents[byte];
        }
    }
    return 1;
}

int fs_snapshot_import(const void *buffer, size_t size)
{
    if (buffer == NULL || size != FS_SNAPSHOT_SIZE) {
        return 0;
    }

    const uint8_t *bytes = buffer;
    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        const uint8_t *record = bytes + index * FS_SNAPSHOT_RECORD_SIZE;
        const uint8_t used = record[FS_SNAPSHOT_USED_OFFSET];
        if (used == 0) {
            for (size_t byte = 0; byte < FS_SNAPSHOT_RECORD_SIZE; ++byte) {
                if (record[byte] != 0) {
                    return 0;
                }
            }
            continue;
        }
        const uint8_t type = record[FS_SNAPSHOT_TYPE_OFFSET];
        const uint16_t parent =
            snapshot_read_u16(record + FS_SNAPSHOT_PARENT_OFFSET);
        const uint8_t name_length = record[FS_SNAPSHOT_NAME_LENGTH_OFFSET];
        const uint16_t node_size =
            snapshot_read_u16(record + FS_SNAPSHOT_SIZE_OFFSET);

        if (used != 1 || (type != FS_NODE_FILE &&
                          type != FS_NODE_DIRECTORY) ||
            name_length == 0 || name_length >= FS_NAME_CAPACITY ||
            node_size > FS_FILE_CAPACITY ||
            (type == FS_NODE_DIRECTORY && node_size != 0) ||
            parent >= FS_NODE_CAPACITY) {
            return 0;
        }
        for (size_t character = 0; character < name_length; ++character) {
            const uint8_t value =
                record[FS_SNAPSHOT_NAME_OFFSET + character];
            if (value == 0 || (index != 0 && value == '/')) {
                return 0;
            }
        }
        for (size_t character = name_length;
             character < FS_NAME_CAPACITY; ++character) {
            if (record[FS_SNAPSHOT_NAME_OFFSET + character] != 0) {
                return 0;
            }
        }
        for (size_t byte = node_size; byte < FS_FILE_CAPACITY; ++byte) {
            if (record[FS_SNAPSHOT_CONTENTS_OFFSET + byte] != 0) {
                return 0;
            }
        }
        if (index == 0) {
            if (type != FS_NODE_DIRECTORY || parent != 0 ||
                name_length != 1 ||
                record[FS_SNAPSHOT_NAME_OFFSET] != '/') {
                return 0;
            }
        }
    }
    if (bytes[0] != 1) {
        return 0;
    }

    for (size_t index = 1; index < FS_NODE_CAPACITY; ++index) {
        const uint8_t *record = bytes + index * FS_SNAPSHOT_RECORD_SIZE;
        if (record[FS_SNAPSHOT_USED_OFFSET] == 0) {
            continue;
        }
        const uint16_t parent =
            snapshot_read_u16(record + FS_SNAPSHOT_PARENT_OFFSET);
        if (parent == index ||
            (record[FS_SNAPSHOT_NAME_LENGTH_OFFSET] == 1 &&
             record[FS_SNAPSHOT_NAME_OFFSET] == '.') ||
            (record[FS_SNAPSHOT_NAME_LENGTH_OFFSET] == 2 &&
             record[FS_SNAPSHOT_NAME_OFFSET] == '.' &&
             record[FS_SNAPSHOT_NAME_OFFSET + 1] == '.')) {
            return 0;
        }
        const uint8_t *parent_record =
            bytes + (size_t)parent * FS_SNAPSHOT_RECORD_SIZE;
        if (parent_record[FS_SNAPSHOT_USED_OFFSET] != 1 ||
            parent_record[FS_SNAPSHOT_TYPE_OFFSET] != FS_NODE_DIRECTORY) {
            return 0;
        }

        size_t ancestor = index;
        size_t depth = 0;
        while (ancestor != 0 && depth < FS_NODE_CAPACITY) {
            const uint8_t *ancestor_record =
                bytes + ancestor * FS_SNAPSHOT_RECORD_SIZE;
            const size_t ancestor_parent = snapshot_read_u16(
                ancestor_record + FS_SNAPSHOT_PARENT_OFFSET);
            if (ancestor_parent >= FS_NODE_CAPACITY ||
                bytes[ancestor_parent * FS_SNAPSHOT_RECORD_SIZE +
                      FS_SNAPSHOT_USED_OFFSET] != 1 ||
                bytes[ancestor_parent * FS_SNAPSHOT_RECORD_SIZE +
                      FS_SNAPSHOT_TYPE_OFFSET] != FS_NODE_DIRECTORY) {
                return 0;
            }
            ancestor = ancestor_parent;
            ++depth;
        }
        if (ancestor != 0) {
            return 0;
        }

        for (size_t other = 1; other < index; ++other) {
            const uint8_t *other_record =
                bytes + other * FS_SNAPSHOT_RECORD_SIZE;
            if (other_record[FS_SNAPSHOT_USED_OFFSET] == 0 ||
                snapshot_read_u16(other_record + FS_SNAPSHOT_PARENT_OFFSET) !=
                    parent ||
                other_record[FS_SNAPSHOT_NAME_LENGTH_OFFSET] !=
                    record[FS_SNAPSHOT_NAME_LENGTH_OFFSET]) {
                continue;
            }
            size_t character = 0;
            const size_t name_length =
                record[FS_SNAPSHOT_NAME_LENGTH_OFFSET];
            while (character < name_length &&
                   other_record[FS_SNAPSHOT_NAME_OFFSET + character] ==
                       record[FS_SNAPSHOT_NAME_OFFSET + character]) {
                ++character;
            }
            if (character == name_length) {
                return 0;
            }
        }
    }

    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        nodes[index].used = 0;
    }
    for (size_t index = 0; index < FS_NODE_CAPACITY; ++index) {
        const uint8_t *record = bytes + index * FS_SNAPSHOT_RECORD_SIZE;
        if (record[FS_SNAPSHOT_USED_OFFSET] == 0) {
            continue;
        }

        struct fs_node *node = &nodes[index];
        const uint8_t name_length = record[FS_SNAPSHOT_NAME_LENGTH_OFFSET];
        const size_t parent =
            snapshot_read_u16(record + FS_SNAPSHOT_PARENT_OFFSET);
        const size_t node_size =
            snapshot_read_u16(record + FS_SNAPSHOT_SIZE_OFFSET);
        node->used = 1;
        node->type = (enum fs_node_type)record[FS_SNAPSHOT_TYPE_OFFSET];
        node->parent = index == 0 ? node : &nodes[parent];
        node->first_child = NULL;
        node->next_sibling = NULL;
        node->size = node_size;
        for (size_t character = 0; character < name_length; ++character) {
            node->name[character] =
                (char)record[FS_SNAPSHOT_NAME_OFFSET + character];
        }
        node->name[name_length] = '\0';
        for (size_t byte = 0; byte < node_size; ++byte) {
            node->contents[byte] =
                record[FS_SNAPSHOT_CONTENTS_OFFSET + byte];
        }
    }
    root = &nodes[0];

    for (size_t index = 1; index < FS_NODE_CAPACITY; ++index) {
        struct fs_node *node = &nodes[index];
        if (!node->used) {
            continue;
        }
        struct fs_node **link = &node->parent->first_child;
        while (*link != NULL) {
            link = &(*link)->next_sibling;
        }
        *link = node;
    }
    return 1;
}
