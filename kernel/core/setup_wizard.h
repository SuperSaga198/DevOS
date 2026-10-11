#ifndef DEVOS_SETUP_WIZARD_H
#define DEVOS_SETUP_WIZARD_H

#include <stdint.h>

#define SETUP_WIZARD_NAME_CAPACITY 32u

enum setup_wizard_state {
    SETUP_WIZARD_COMPLETE = 0,
    SETUP_WIZARD_USERNAME = 1,
    SETUP_WIZARD_HOSTNAME = 2,
    SETUP_WIZARD_COMMIT = 3,
    SETUP_WIZARD_SAVE_ERROR = 4,
};

struct setup_profile {
    char username[SETUP_WIZARD_NAME_CAPACITY];
    char hostname[SETUP_WIZARD_NAME_CAPACITY];
};

void setup_wizard_init(void);
enum setup_wizard_state setup_wizard_get_state(void);
const struct setup_profile *setup_wizard_get_profile(void);
const char *setup_wizard_get_input(void);
void setup_wizard_input(uint16_t key);
void setup_wizard_persist_result(int result);

#endif
