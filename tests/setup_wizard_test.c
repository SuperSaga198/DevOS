#include "fs.h"
#include "setup_wizard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void type_name(const char *name)
{
    while (*name != '\0') {
        setup_wizard_input((uint8_t)*name++);
    }
    setup_wizard_input('\r');
}

int main(void)
{
    static uint8_t snapshot[FS_SNAPSHOT_SIZE];
    fs_init();
    setup_wizard_init();
    assert(setup_wizard_get_state() == SETUP_WIZARD_USERNAME);

    setup_wizard_input('!');
    setup_wizard_input('d');
    setup_wizard_input('e');
    setup_wizard_input('v');
    setup_wizard_input(0x101);
    setup_wizard_input('v');
    setup_wizard_input('\r');
    assert(setup_wizard_get_state() == SETUP_WIZARD_HOSTNAME);
    type_name("dev-machine");
    assert(setup_wizard_get_state() == SETUP_WIZARD_COMMIT);
    assert(strcmp(setup_wizard_get_profile()->username, "dev") == 0);
    assert(strcmp(setup_wizard_get_profile()->hostname, "dev-machine") == 0);
    setup_wizard_persist_result(0);
    assert(setup_wizard_get_state() == SETUP_WIZARD_SAVE_ERROR);
    setup_wizard_input('\r');
    assert(setup_wizard_get_state() == SETUP_WIZARD_COMMIT);
    setup_wizard_persist_result(1);
    assert(setup_wizard_get_state() == SETUP_WIZARD_COMPLETE);

    assert(fs_snapshot_export(snapshot, sizeof(snapshot)));
    fs_init();
    assert(fs_snapshot_import(snapshot, sizeof(snapshot)));
    setup_wizard_init();
    assert(setup_wizard_get_state() == SETUP_WIZARD_COMPLETE);
    assert(strcmp(setup_wizard_get_profile()->username, "dev") == 0);
    assert(strcmp(setup_wizard_get_profile()->hostname, "dev-machine") == 0);

    assert(fs_write("/etc/devos.conf", "DEVOS1\nUSER=dev\nHOST=host\nextra\n",
                    sizeof("DEVOS1\nUSER=dev\nHOST=host\nextra\n") - 1));
    setup_wizard_init();
    assert(setup_wizard_get_state() == SETUP_WIZARD_USERNAME);

    char long_name[SETUP_WIZARD_NAME_CAPACITY];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    type_name(long_name);
    memset(long_name, 'b', sizeof(long_name) - 1);
    type_name(long_name);
    assert(setup_wizard_get_state() == SETUP_WIZARD_COMMIT);
    assert(strlen(setup_wizard_get_profile()->username) == 31);
    assert(strlen(setup_wizard_get_profile()->hostname) == 31);
    assert(fs_size(fs_lookup("/etc/devos.conf")) == 81);

    puts("setup wizard tests passed");
    return 0;
}
