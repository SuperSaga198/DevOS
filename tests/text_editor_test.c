#include "../kernel/core/fs.h"
#include "../kernel/core/text_editor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    fs_init();
    assert(fs_create("/notes.txt"));
    assert(fs_write("/notes.txt", "old", 3));

    assert(text_editor_open("/notes.txt"));
    assert(strcmp(text_editor_path(), "/notes.txt") == 0);
    assert(strcmp(text_editor_contents(), "old") == 0);
    assert(!text_editor_is_dirty());
    assert(text_editor_input(0x101) == TEXT_EDITOR_CHANGED);
    assert(strcmp(text_editor_contents(), "ol") == 0);
    assert(text_editor_input('\n') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('!') == TEXT_EDITOR_CHANGED);
    assert(text_editor_is_dirty());
    assert(text_editor_input(TEXT_EDITOR_KEY_SAVE) == TEXT_EDITOR_SAVED);
    assert(!text_editor_is_dirty());

    char saved[16];
    size_t saved_size = 0;
    assert(fs_read("/notes.txt", saved, sizeof(saved), &saved_size));
    assert(saved_size == 4);
    assert(memcmp(saved, "ol\n!", saved_size) == 0);

    assert(text_editor_input(TEXT_EDITOR_KEY_QUIT) == TEXT_EDITOR_CLOSED);
    assert(!text_editor_is_open());
    assert(text_editor_input('x') == TEXT_EDITOR_IGNORED);

    assert(text_editor_open("/new.txt"));
    assert(text_editor_size() == 0);
    for (size_t index = 0; index < TEXT_EDITOR_CAPACITY; ++index) {
        assert(text_editor_input('x') == TEXT_EDITOR_CHANGED);
    }
    assert(text_editor_input('y') == TEXT_EDITOR_ERROR);
    assert(text_editor_input(TEXT_EDITOR_KEY_SAVE) == TEXT_EDITOR_SAVED);
    assert(fs_size(fs_lookup("/new.txt")) == TEXT_EDITOR_CAPACITY);

    assert(text_editor_open("/command.txt"));
    assert(text_editor_input('o') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('k') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('\n') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input(':') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('w') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('\n') == TEXT_EDITOR_SAVED);
    assert(strcmp(text_editor_contents(), "ok\n") == 0);
    assert(text_editor_input(':') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('q') == TEXT_EDITOR_CHANGED);
    assert(text_editor_input('\n') == TEXT_EDITOR_CLOSED);

    assert(!text_editor_open("relative.txt"));
    assert(!text_editor_open("/"));
    puts("text editor tests passed");
    return 0;
}
