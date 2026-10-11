#include "setup_wizard.h"

#include "fs.h"

#include <stddef.h>

#define SETUP_CONFIG_PATH "/etc/devos.conf"
#define SETUP_CONFIG_CAPACITY 82u

static enum setup_wizard_state state;
static struct setup_profile profile;
static char input[SETUP_WIZARD_NAME_CAPACITY];
static size_t input_length;

static void setup_zero(void *destination, size_t size)
{
    uint8_t *bytes = destination;
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void setup_copy(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input_bytes = source;
    for (size_t index = 0; index < size; ++index) {
        output[index] = input_bytes[index];
    }
}

static int setup_equal(const char *left, const char *right, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int valid_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (size_t index = 0; name[index] != '\0'; ++index) {
        const char value = name[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '-')) {
            return 0;
        }
    }
    return 1;
}

static int load_profile(void)
{
    char contents[SETUP_CONFIG_CAPACITY];
    size_t size = 0;
    if (!fs_read(SETUP_CONFIG_PATH, contents, sizeof(contents) - 1, &size)) {
        return 0;
    }
    contents[size] = '\0';

    const char *prefix = "DEVOS1\nUSER=";
    const size_t prefix_length = sizeof("DEVOS1\nUSER=") - 1;
    if (size < prefix_length ||
        !setup_equal(contents, prefix, prefix_length)) {
        return 0;
    }
    size_t position = prefix_length;
    size_t user_length = 0;
    while (position < size && contents[position] != '\n' &&
           user_length + 1 < sizeof(profile.username)) {
        profile.username[user_length++] = contents[position++];
    }
    profile.username[user_length] = '\0';
    if (position >= size || contents[position++] != '\n' ||
        size - position < sizeof("HOST=") - 1 ||
        !setup_equal(contents + position, "HOST=", sizeof("HOST=") - 1)) {
        return 0;
    }
    position += sizeof("HOST=") - 1;
    size_t host_length = 0;
    while (position < size && contents[position] != '\n' &&
           host_length + 1 < sizeof(profile.hostname)) {
        profile.hostname[host_length++] = contents[position++];
    }
    profile.hostname[host_length] = '\0';
    if (position >= size || contents[position++] != '\n' || position != size) {
        return 0;
    }
    return valid_name(profile.username) && valid_name(profile.hostname);
}

static int persist_profile(void)
{
    char contents[SETUP_CONFIG_CAPACITY];
    size_t position = 0;
    const char *header = "DEVOS1\nUSER=";
    while (*header != '\0') {
        contents[position++] = *header++;
    }
    for (size_t index = 0; profile.username[index] != '\0'; ++index) {
        contents[position++] = profile.username[index];
    }
    contents[position++] = '\n';
    const char *host_prefix = "HOST=";
    while (*host_prefix != '\0') {
        contents[position++] = *host_prefix++;
    }
    for (size_t index = 0; profile.hostname[index] != '\0'; ++index) {
        contents[position++] = profile.hostname[index];
    }
    contents[position++] = '\n';

    if (fs_lookup("/etc") == NULL && !fs_mkdir("/etc")) {
        return 0;
    }
    if (fs_lookup(SETUP_CONFIG_PATH) == NULL &&
        !fs_create(SETUP_CONFIG_PATH)) {
        return 0;
    }
    return fs_write(SETUP_CONFIG_PATH, contents, position);
}

void setup_wizard_init(void)
{
    setup_zero(&profile, sizeof(profile));
    setup_zero(input, sizeof(input));
    input_length = 0;
    if (load_profile()) {
        state = SETUP_WIZARD_COMPLETE;
    } else {
        state = SETUP_WIZARD_USERNAME;
    }
}

enum setup_wizard_state setup_wizard_get_state(void)
{
    return state;
}

const struct setup_profile *setup_wizard_get_profile(void)
{
    return &profile;
}

const char *setup_wizard_get_input(void)
{
    return input;
}

void setup_wizard_input(uint16_t key)
{
    if (state == SETUP_WIZARD_SAVE_ERROR) {
        if (key == '\n' || key == '\r' || key == 0x100) {
            if (persist_profile()) {
                state = SETUP_WIZARD_COMMIT;
            }
        }
        return;
    }
    if (state != SETUP_WIZARD_USERNAME &&
        state != SETUP_WIZARD_HOSTNAME) {
        return;
    }

    if (key == 0x101 || key == '\b' || key == 0x7f) {
        if (input_length > 0) {
            input[--input_length] = '\0';
        }
        return;
    }
    if (key != '\n' && key != '\r' && key != 0x100) {
        if (input_length + 1 < sizeof(input) &&
            ((key >= 'a' && key <= 'z') ||
             (key >= 'A' && key <= 'Z') ||
             (key >= '0' && key <= '9') ||
             key == '-')) {
            input[input_length++] = (char)key;
            input[input_length] = '\0';
        }
        return;
    }

    if (!valid_name(input)) {
        return;
    }
    if (state == SETUP_WIZARD_USERNAME) {
        setup_copy(profile.username, input, input_length + 1);
        setup_zero(input, sizeof(input));
        input_length = 0;
        state = SETUP_WIZARD_HOSTNAME;
        return;
    }

    setup_copy(profile.hostname, input, input_length + 1);
    if (persist_profile()) {
        state = SETUP_WIZARD_COMMIT;
    } else {
        state = SETUP_WIZARD_SAVE_ERROR;
    }
}

void setup_wizard_persist_result(int result)
{
    if (state != SETUP_WIZARD_COMMIT) {
        return;
    }
    state = result ? SETUP_WIZARD_COMPLETE : SETUP_WIZARD_SAVE_ERROR;
}
