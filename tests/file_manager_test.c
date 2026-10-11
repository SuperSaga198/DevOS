#include "file_manager.h"
#include "fs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    fs_init();
    assert(fs_mkdir("/projects"));
    assert(fs_create("/projects/notes.txt"));
    assert(fs_write("/projects/notes.txt", "Build DevOS", 11));
    assert(fs_create("/welcome.txt"));
    assert(fs_write("/welcome.txt", "Welcome", 7));

    file_manager_init();
    assert(strcmp(file_manager_path(), "/") == 0);
    assert(file_manager_entry_count() == 2);
    assert(file_manager_entry_at(0)->is_directory);
    assert(strcmp(file_manager_entry_at(0)->name, "projects") == 0);

    assert(file_manager_open_entry(0));
    assert(strcmp(file_manager_path(), "/projects") == 0);
    assert(file_manager_entry_count() == 1);
    assert(strcmp(file_manager_entry_at(0)->name, "notes.txt") == 0);

    assert(file_manager_open_entry(0));
    assert(strcmp(file_manager_preview(), "Build DevOS") == 0);
    assert(!file_manager_open_entry(1));
    assert(fs_write("/projects/notes.txt", "Updated", 7));
    file_manager_refresh();
    assert(file_manager_preview()[0] == '\0');
    assert(file_manager_open_entry(0));
    assert(strcmp(file_manager_preview(), "Updated") == 0);

    file_manager_parent();
    assert(strcmp(file_manager_path(), "/") == 0);
    assert(file_manager_open_entry(1));
    assert(strcmp(file_manager_preview(), "Welcome") == 0);

    puts("file manager tests passed");
    return 0;
}
