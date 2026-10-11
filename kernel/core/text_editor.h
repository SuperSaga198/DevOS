#ifndef DEVOS_TEXT_EDITOR_H
#define DEVOS_TEXT_EDITOR_H

#include <stddef.h>
#include <stdint.h>

#define TEXT_EDITOR_CAPACITY 2048u
#define TEXT_EDITOR_PATH_CAPACITY 64u
#define TEXT_EDITOR_KEY_SAVE 0x13u
#define TEXT_EDITOR_KEY_QUIT 0x11u

enum text_editor_result {
    TEXT_EDITOR_IGNORED,
    TEXT_EDITOR_CHANGED,
    TEXT_EDITOR_SAVED,
    TEXT_EDITOR_CLOSED,
    TEXT_EDITOR_ERROR,
};

int text_editor_open(const char *path);
enum text_editor_result text_editor_input(uint16_t key);
int text_editor_save(void);
int text_editor_is_open(void);
int text_editor_is_dirty(void);
const char *text_editor_path(void);
const char *text_editor_contents(void);
size_t text_editor_size(void);

#endif
