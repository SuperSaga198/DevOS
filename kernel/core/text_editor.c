#include "text_editor.h"

#include "fs.h"

static char editor_path[TEXT_EDITOR_PATH_CAPACITY];
static char editor_contents[TEXT_EDITOR_CAPACITY + 1];
static size_t editor_size;
static uint8_t editor_open;
static uint8_t editor_dirty;

static void close_editor(void)
{
    editor_open = 0;
    editor_dirty = 0;
    editor_size = 0;
    editor_contents[0] = '\0';
    editor_path[0] = '\0';
}

static size_t current_line_start(void)
{
    size_t start = editor_size;
    while (start != 0 && editor_contents[start - 1] != '\n') {
        --start;
    }
    return start;
}

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

int text_editor_open(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return 0;
    }
    const size_t path_length = string_length(path);
    if (path_length == 0 || path_length >= sizeof(editor_path)) {
        return 0;
    }

    struct fs_node *node = fs_lookup(path);
    if (node == NULL) {
        if (!fs_create(path)) {
            return 0;
        }
    } else if (fs_type(node) != FS_NODE_FILE) {
        return 0;
    }

    size_t size = 0;
    if (!fs_read(path, editor_contents, TEXT_EDITOR_CAPACITY, &size)) {
        return 0;
    }
    for (size_t index = 0; index < path_length; ++index) {
        editor_path[index] = path[index];
    }
    editor_path[path_length] = '\0';
    editor_size = size;
    editor_contents[editor_size] = '\0';
    editor_open = 1;
    editor_dirty = 0;
    return 1;
}

int text_editor_save(void)
{
    if (!editor_open || !fs_write(editor_path, editor_contents, editor_size)) {
        return 0;
    }
    editor_dirty = 0;
    return 1;
}

enum text_editor_result text_editor_input(uint16_t key)
{
    if (!editor_open) {
        return TEXT_EDITOR_IGNORED;
    }
    if (key == TEXT_EDITOR_KEY_SAVE) {
        return text_editor_save() ? TEXT_EDITOR_SAVED : TEXT_EDITOR_ERROR;
    }
    if (key == TEXT_EDITOR_KEY_QUIT) {
        close_editor();
        return TEXT_EDITOR_CLOSED;
    }
    if (key == '\r' || key == '\n' || key == 0x100) {
        const size_t line_start = current_line_start();
        const size_t line_size = editor_size - line_start;
        if (line_size == 2 && editor_contents[line_start] == ':' &&
            editor_contents[line_start + 1] == 'w') {
            editor_size = line_start;
            editor_contents[editor_size] = '\0';
            editor_dirty = 1;
            return text_editor_save() ? TEXT_EDITOR_SAVED :
                                        TEXT_EDITOR_ERROR;
        }
        if (line_size == 2 && editor_contents[line_start] == ':' &&
            editor_contents[line_start + 1] == 'q') {
            close_editor();
            return TEXT_EDITOR_CLOSED;
        }
        key = '\n';
    } else if (key == '\b' || key == 0x7f || key == 0x101) {
        if (editor_size == 0) {
            return TEXT_EDITOR_IGNORED;
        }
        editor_contents[--editor_size] = '\0';
        editor_dirty = 1;
        return TEXT_EDITOR_CHANGED;
    } else if (key < 0x20 || key > 0x7e) {
        return TEXT_EDITOR_IGNORED;
    }

    if (editor_size >= TEXT_EDITOR_CAPACITY) {
        return TEXT_EDITOR_ERROR;
    }
    editor_contents[editor_size++] = (char)key;
    editor_contents[editor_size] = '\0';
    editor_dirty = 1;
    return TEXT_EDITOR_CHANGED;
}

int text_editor_is_open(void)
{
    return editor_open;
}

int text_editor_is_dirty(void)
{
    return editor_dirty;
}

const char *text_editor_path(void)
{
    return editor_open ? editor_path : "";
}

const char *text_editor_contents(void)
{
    return editor_contents;
}

size_t text_editor_size(void)
{
    return editor_size;
}
