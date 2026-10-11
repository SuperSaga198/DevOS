#include "file_manager.h"

#include "fs.h"

static char current_path[FILE_MANAGER_PATH_CAPACITY] = "/";
static char preview[FILE_MANAGER_PREVIEW_CAPACITY];
static struct file_manager_entry entries[FILE_MANAGER_ENTRY_LIMIT];
static size_t entry_count;
static uint8_t file_buffer[2048];

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t index = 0;
    if (destination == NULL || capacity == 0) {
        return;
    }
    while (source != NULL && source[index] != '\0' && index + 1 < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void file_manager_init(void)
{
    copy_text(current_path, sizeof(current_path), "/");
    preview[0] = '\0';
    file_manager_refresh();
}

void file_manager_refresh(void)
{
    preview[0] = '\0';
    entry_count = 0;
    for (size_t index = 0; index < FILE_MANAGER_ENTRY_LIMIT; ++index) {
        entries[index].name[0] = '\0';
        entries[index].size = 0;
        entries[index].is_directory = 0;
    }

    struct fs_node *directory = fs_lookup(current_path);
    if (directory == NULL || fs_type(directory) != FS_NODE_DIRECTORY) {
        return;
    }

    while (entry_count < FILE_MANAGER_ENTRY_LIMIT) {
        struct fs_node *child = fs_child(directory, entry_count);
        if (child == NULL) {
            break;
        }

        copy_text(entries[entry_count].name, sizeof(entries[entry_count].name),
                  fs_name(child));
        entries[entry_count].is_directory =
            fs_type(child) == FS_NODE_DIRECTORY;
        entries[entry_count].size = (uint32_t)fs_size(child);
        ++entry_count;
    }
}

void file_manager_parent(void)
{
    size_t length = string_length(current_path);
    if (length <= 1) {
        preview[0] = '\0';
        return;
    }
    while (length > 1 && current_path[length - 1] == '/') {
        --length;
    }
    while (length > 1 && current_path[length - 1] != '/') {
        --length;
    }
    if (length <= 1) {
        current_path[1] = '\0';
    } else {
        current_path[length - 1] = '\0';
    }
    preview[0] = '\0';
    file_manager_refresh();
}

static int append_name(const char *name)
{
    const size_t path_length = string_length(current_path);
    const size_t name_length = string_length(name);
    const size_t separator = path_length > 1 ? 1 : 0;
    if (path_length + separator + name_length >= sizeof(current_path)) {
        return 0;
    }

    size_t position = path_length;
    if (separator != 0) {
        current_path[position++] = '/';
    }
    for (size_t index = 0; index < name_length; ++index) {
        current_path[position++] = name[index];
    }
    current_path[position] = '\0';
    return 1;
}

static int preview_file(const char *name)
{
    char path[FILE_MANAGER_PATH_CAPACITY];
    size_t path_length = string_length(current_path);
    size_t position = 0;
    for (size_t index = 0; index < path_length && index + 1 < sizeof(path); ++index) {
        path[position++] = current_path[index];
    }
    if (position > 1 && path[position - 1] != '/') {
        path[position++] = '/';
    }
    for (size_t index = 0; name[index] != '\0' &&
         position + 1 < sizeof(path); ++index) {
        path[position++] = name[index];
    }
    path[position] = '\0';

    size_t file_size = 0;
    if (!fs_read(path, file_buffer, sizeof(file_buffer), &file_size)) {
        copy_text(preview, sizeof(preview), "Unable to read file");
        return 0;
    }

    size_t preview_size = file_size;
    if (preview_size >= sizeof(preview)) {
        preview_size = sizeof(preview) - 4;
    }
    for (size_t index = 0; index < preview_size; ++index) {
        const uint8_t character = file_buffer[index];
        preview[index] = character == '\n' || character == '\r' ? ' ' :
            (character >= 0x20 && character <= 0x7e) ? (char)character : '?';
    }
    if (preview_size < file_size) {
        preview[preview_size++] = '.';
        preview[preview_size++] = '.';
        preview[preview_size++] = '.';
    }
    preview[preview_size] = '\0';
    return 1;
}

int file_manager_open_entry(size_t index)
{
    if (index >= entry_count) {
        return 0;
    }
    if (entries[index].is_directory) {
        if (!append_name(entries[index].name)) {
            return 0;
        }
        preview[0] = '\0';
        file_manager_refresh();
        return 1;
    }
    return preview_file(entries[index].name);
}

const char *file_manager_path(void)
{
    return current_path;
}

const char *file_manager_preview(void)
{
    return preview;
}

size_t file_manager_entry_count(void)
{
    return entry_count;
}

const struct file_manager_entry *file_manager_entry_at(size_t index)
{
    return index < entry_count ? &entries[index] : NULL;
}
