#include "framebuffer.h"
#include "interrupts.h"
#include "keyboard.h"
#include "limine.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = {
    0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = {
    0xf9562b2d5c95a6c8,
    0x6a7b384944536bdc,
    3,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = { LIMINE_COMMON_MAGIC, LIMINE_FRAMEBUFFER_REQUEST_ID },
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = { LIMINE_COMMON_MAGIC, LIMINE_MEMMAP_REQUEST_ID },
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = {
    0xadc0e0531bb10d03,
    0x9572709f31764c62,
};

static void serial_write_uint(uint64_t value)
{
    char digits[20];
    size_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && length < sizeof(digits));

    while (length > 0) {
        char character[2] = { digits[--length], '\0' };
        serial_write(character);
    }
}

static uint64_t usable_memory_mib(void)
{
    struct limine_memmap_response *response = memmap_request.response;
    if (response == NULL || response->entries == NULL) {
        return 0;
    }

    uint64_t total_bytes = 0;
    for (uint64_t index = 0; index < response->entry_count; ++index) {
        struct limine_memmap_entry *entry = response->entries[index];
        if (entry != NULL && entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length > UINT64_MAX - total_bytes) {
                return UINT64_MAX / (1024 * 1024);
            }
            total_bytes += entry->length;
        }
    }
    return total_bytes / (1024 * 1024);
}

static char command[64];
static size_t command_length;
static uint64_t detected_memory_mib;
static uint8_t terminal_open;
static uint8_t last_serial_was_cr;

static void shell_write(const char *text)
{
    serial_write(text);
    framebuffer_terminal_write(text);
}

static void shell_write_uint(uint64_t value)
{
    char digits[20];
    size_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && length < sizeof(digits));

    while (length > 0) {
        char character[2] = { digits[--length], '\0' };
        shell_write(character);
    }
}

static uint8_t command_is(const char *expected)
{
    size_t index = 0;
    while (command[index] != '\0' && expected[index] != '\0') {
        char actual = command[index];
        char wanted = expected[index];
        if (actual >= 'A' && actual <= 'Z') {
            actual = (char)(actual - 'A' + 'a');
        }
        if (wanted >= 'A' && wanted <= 'Z') {
            wanted = (char)(wanted - 'A' + 'a');
        }
        if (actual != wanted) {
            return 0;
        }
        ++index;
    }
    return command[index] == '\0' && expected[index] == '\0';
}

static void shell_prompt(void)
{
    shell_write("devos> ");
}

static void shell_execute(void)
{
    command[command_length] = '\0';
    if (command_is("help")) {
        shell_write("Commands:\r\n");
        shell_write("  help  - show this help\r\n");
        shell_write("  mem   - show usable memory\r\n");
        shell_write("  ver   - show kernel version\r\n");
        shell_write("  clear - clear the terminal\r\n");
        shell_write("  home  - return to the desktop\r\n");
    } else if (command_is("mem")) {
        shell_write("Usable memory: ");
        shell_write_uint(detected_memory_mib);
        shell_write(" MiB\r\n");
    } else if (command_is("ver")) {
        shell_write("DevOS developer preview, x86-64\r\n");
    } else if (command_is("clear")) {
        framebuffer_terminal_clear();
    } else if (command_is("home")) {
        terminal_open = 0;
        desktop_draw(detected_memory_mib);
        command_length = 0;
        return;
    } else if (command_length != 0) {
        shell_write("Unknown command. Type help.\r\n");
    }

    command_length = 0;
    shell_prompt();
}

static void shell_input(uint16_t key)
{
    if (key == KEYBOARD_KEY_ENTER || key == '\r' || key == '\n') {
        shell_write("\r\n");
        shell_execute();
        return;
    }
    if (key == KEYBOARD_KEY_BACKSPACE || key == '\b' || key == 0x7f) {
        if (command_length != 0) {
            --command_length;
            command[command_length] = '\0';
            framebuffer_terminal_backspace();
            serial_write("\b \b");
        }
        return;
    }
    if (key >= 0x20 && key <= 0x7e &&
        command_length < sizeof(command) - 1) {
        command[command_length++] = (char)key;
        command[command_length] = '\0';
        char echo[2] = { (char)key, '\0' };
        shell_write(echo);
    }
}

static void kernel_shell(void)
{
    shell_write("Press T to open the terminal; type help for commands.\r\n");
    for (;;) {
        __asm__ volatile ("cli");
        uint16_t key;
        uint8_t serial_character;
        const int have_key = keyboard_get_key(&key);
        const int have_serial_character = serial_get_char(&serial_character);
        if (!have_key && !have_serial_character) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        __asm__ volatile ("sti");

        if (have_key) {
            if (!terminal_open) {
                if (key == 't' || key == 'T') {
                    terminal_open = 1;
                    command_length = 0;
                    framebuffer_terminal_open();
                    shell_write("DevOS terminal. Type help for commands.\r\n");
                    shell_prompt();
                }
            } else {
                shell_input(key);
            }
        }

        if (have_serial_character) {
            const uint8_t input = serial_character;
            if (input == '\n' && last_serial_was_cr) {
                last_serial_was_cr = 0;
                continue;
            }
            last_serial_was_cr = input == '\r';
            if (!terminal_open) {
                terminal_open = 1;
                command_length = 0;
                framebuffer_terminal_open();
                shell_write("DevOS terminal. Type help for commands.\r\n");
                shell_prompt();
            }
            shell_input(input);
        }
    }
}

void kernel_main(void)
{
    serial_init();
    serial_write("DevOS kernel started (x86-64)\r\n");
    detected_memory_mib = usable_memory_mib();
    serial_write("Usable memory: ");
    serial_write_uint(detected_memory_mib);
    serial_write(" MiB\r\n");

    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (response != NULL && response->framebuffers != NULL &&
        response->framebuffer_count > 0 &&
        framebuffer_init(response->framebuffers[0])) {
        desktop_draw(detected_memory_mib);
        serial_write("Framebuffer desktop initialized.\r\n");
    } else {
        serial_write("Framebuffer unavailable; using serial console.\r\n");
    }

    interrupts_init();
    kernel_shell();
}
