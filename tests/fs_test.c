#include "../kernel/core/fs.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    fs_init();
    assert(fs_root() != NULL);
    assert(fs_type(fs_root()) == FS_NODE_DIRECTORY);
    assert(fs_lookup("/") == fs_root());
    assert(fs_lookup("relative") == NULL);

    assert(fs_mkdir("/home"));
    assert(fs_mkdir("/home/dev"));
    assert(fs_create("/home/dev/readme.txt"));
    assert(!fs_create("/home/dev/readme.txt"));
    assert(fs_lookup("/home/dev/../dev/readme.txt") != NULL);
    assert(fs_lookup("/home/dev/../../") == fs_root());
    assert(fs_lookup("/home/missing") == NULL);
    assert(!fs_create("/missing/file"));
    assert(!fs_mkdir("/home/dev/readme.txt/child"));

    static const char message[] = "Hello DevOS\n";
    assert(fs_write("/home/dev/readme.txt", message, sizeof(message) - 1));
    assert(fs_size(fs_lookup("/home/dev/readme.txt")) == sizeof(message) - 1);

    char buffer[32] = { 0 };
    size_t size = 99;
    assert(!fs_read("/home/dev/readme.txt", buffer, 2, &size));
    assert(size == 99);
    assert(fs_read("/home/dev/readme.txt", buffer, sizeof(buffer), &size));
    assert(size == sizeof(message) - 1);
    for (size_t index = 0; index < size; ++index) {
        assert(buffer[index] == message[index]);
    }

    assert(fs_child(fs_lookup("/home/dev"), 0) ==
           fs_lookup("/home/dev/readme.txt"));
    assert(fs_child(fs_lookup("/home/dev"), 1) == NULL);
    assert(!fs_remove("/"));
    assert(!fs_remove("/home"));
    assert(fs_remove("/home/dev/readme.txt"));
    assert(fs_lookup("/home/dev/readme.txt") == NULL);
    assert(fs_remove("/home/dev"));
    assert(fs_remove("/home"));

    uint8_t snapshot[FS_SNAPSHOT_SIZE];
    memset(snapshot, 0, sizeof(snapshot));
    assert(fs_snapshot_export(snapshot, sizeof(snapshot)));
    for (size_t index = 0; index < 64; ++index) {
        const size_t offset = index * FS_SNAPSHOT_RECORD_SIZE;
        if (snapshot[offset] == 0) {
            snapshot[offset] = 1;
            assert(!fs_snapshot_import(snapshot, sizeof(snapshot)));
            snapshot[offset] = 0;
            break;
        }
    }

    for (uint32_t index = 0; index < 63; ++index) {
        char path[] = "/node000";
        path[5] = (char)('0' + (index / 100) % 10);
        path[6] = (char)('0' + (index / 10) % 10);
        path[7] = (char)('0' + index % 10);
        assert(fs_create(path));
    }
    assert(!fs_create("/out-of-nodes"));
    assert(fs_remove("/node000"));
    assert(fs_create("/reused-node"));

    puts("filesystem tests passed");
    return 0;
}
