#include "framebuffer.h"
#include "fs.h"
#include "interrupts.h"
#include "keyboard.h"
#include "limine.h"
#include "memory.h"
#include "mouse.h"
#include "serial.h"
#include "timer.h"
#include "fs_disk.h"
#include "elf64.h"
#include "ata_pio.h"
#include "pci.h"

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = { LIMINE_COMMON_MAGIC, LIMINE_HHDM_REQUEST_ID },
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
static int allocator_available;
static uint64_t shell_allocated_page;
static int shell_has_allocated_page;
static int storage_available;
static int pointer_available;
static uint64_t pointer_x;
static uint64_t pointer_y;
static uint8_t pointer_buttons;

static void shell_write(const char *text)
{
    serial_write(text);
    framebuffer_terminal_write(text);
}

static void persist_filesystem_change(void)
{
    if (storage_available && !fs_disk_save()) {
        shell_write("warning: change is only in RAM; disk save failed\r\n");
    }
}

static void shell_write_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char output[17];
    size_t length = 0;
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = (uint8_t)((value >> shift) & 0x0f);
        if (digit != 0 || started || shift == 0) {
            output[length++] = digits[digit];
            started = 1;
        }
    }
    output[length] = '\0';
    shell_write(output);
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

static char *skip_spaces(char *text)
{
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    return text;
}

static char *take_token(char **cursor)
{
    char *text = skip_spaces(*cursor);
    *cursor = text;
    if (*text == '\0') {
        return NULL;
    }
    char *token = text;
    while (*text != '\0' && *text != ' ' && *text != '\t') {
        ++text;
    }
    if (*text != '\0') {
        *text++ = '\0';
    }
    *cursor = text;
    return token;
}

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void shell_list(const char *path)
{
    struct fs_node *directory = fs_lookup(path);
    if (directory == NULL) {
        shell_write("ls: path not found\r\n");
        return;
    }
    if (fs_type(directory) != FS_NODE_DIRECTORY) {
        shell_write(fs_name(directory));
        shell_write("\r\n");
        return;
    }

    for (size_t index = 0;; ++index) {
        struct fs_node *child = fs_child(directory, index);
        if (child == NULL) {
            break;
        }
        shell_write(fs_type(child) == FS_NODE_DIRECTORY ? "d " : "f ");
        shell_write(fs_name(child));
        if (fs_type(child) == FS_NODE_FILE) {
            shell_write("  ");
            shell_write_uint(fs_size(child));
            shell_write(" bytes");
        }
        shell_write("\r\n");
    }
}

static void shell_cat(const char *path)
{
    char contents[2048];
    size_t size;
    if (!fs_read(path, contents, sizeof(contents), &size)) {
        shell_write("cat: unable to read file\r\n");
        return;
    }
    for (size_t index = 0; index < size; ++index) {
        const char character = contents[index];
        if (character == '\n') {
            shell_write("\r\n");
        } else if (character == '\r' ||
                   (character >= 0x20 && character <= 0x7e)) {
            char output[2] = { character, '\0' };
            shell_write(output);
        } else {
            shell_write("?");
        }
    }
    if (size == 0 || contents[size - 1] != '\n') {
        shell_write("\r\n");
    }
}

static void shell_elfcheck(const char *path)
{
    uint8_t image[2048];
    size_t image_size;
    struct elf64_info info;
    if (path == NULL ||
        !fs_read(path, image, sizeof(image), &image_size) ||
        !elf64_inspect(image, image_size, &info)) {
        shell_write("elfcheck: invalid or unsupported ELF64 executable\r\n");
        return;
    }

    shell_write("ELF64 x86-64 executable; entry=0x");
    shell_write_hex(info.entry);
    shell_write("; load segments: ");
    shell_write_uint(info.segment_count);
    shell_write("\r\n");
}

static void pointer_apply_event(const struct mouse_event *event)
{
    const uint64_t max_x = framebuffer_width() - 1;
    const uint64_t max_y = framebuffer_height() - 1;
    if (event->delta_x >= 0) {
        const uint64_t delta = (uint16_t)event->delta_x;
        pointer_x = delta > max_x - pointer_x ? max_x : pointer_x + delta;
    } else {
        const uint64_t delta = (uint64_t)(-(int32_t)event->delta_x);
        pointer_x = delta > pointer_x ? 0 : pointer_x - delta;
    }
    if (event->delta_y >= 0) {
        const uint64_t delta = (uint16_t)event->delta_y;
        pointer_y = delta > max_y - pointer_y ? max_y : pointer_y + delta;
    } else {
        const uint64_t delta = (uint64_t)(-(int32_t)event->delta_y);
        pointer_y = delta > pointer_y ? 0 : pointer_y - delta;
    }
    pointer_buttons = event->buttons;
    framebuffer_cursor_set(pointer_x, pointer_y);
}

static void shell_prompt(void)
{
    shell_write("devos> ");
}

static void shell_execute(void)
{
    command[command_length] = '\0';
    char *arguments = skip_spaces(command);
    if (arguments != command) {
        size_t index = 0;
        while (arguments[index] != '\0') {
            command[index] = arguments[index];
            ++index;
        }
        command[index] = '\0';
        command_length = index;
    }
    arguments = command;
    char *end = arguments;
    while (*end != '\0' && *end != ' ' && *end != '\t') {
        ++end;
    }
    char saved_separator = *end;
    *end = '\0';
    char *arguments_end = end + (saved_separator != '\0');

    if (command_is("help")) {
        shell_write("Commands:\r\n");
        shell_write("  help  - show this help\r\n");
        shell_write("  mem   - show usable memory\r\n");
        shell_write("  ver   - show kernel version\r\n");
        shell_write("  clear - clear the terminal\r\n");
        shell_write("  home  - return to the desktop\r\n");
        shell_write("  ls [PATH]       - list a RAM filesystem directory\r\n");
        shell_write("  mkdir PATH      - create a directory\r\n");
        shell_write("  touch PATH      - create an empty file\r\n");
        shell_write("  write PATH TEXT - replace a file's contents\r\n");
        shell_write("  cat PATH        - print a file\r\n");
        shell_write("  elfcheck PATH   - validate an ELF64 x86-64 executable\r\n");
        shell_write("  rm PATH         - remove an empty file or directory\r\n");
        shell_write("  uptime          - show time since boot\r\n");
        shell_write("  disk            - show persistent storage status\r\n");
        shell_write("  pointer         - show mouse input status\r\n");
        shell_write("  pagealloc       - allocate a physical page\r\n");
        shell_write("  pagefree        - free the page from pagealloc\r\n");
        shell_write("  fault           - trigger a diagnostic CPU fault\r\n");
        shell_write("  faultpf         - trigger a diagnostic page fault\r\n");
    } else if (command_is("mem")) {
        struct memory_stats stats;
        shell_write("Bootloader usable memory: ");
        shell_write_uint(detected_memory_mib);
        shell_write(" MiB\r\n");
        if (memory_get_stats(&stats)) {
            shell_write("Managed pages: ");
            shell_write_uint(stats.managed_pages);
            shell_write(" (");
            shell_write_uint(stats.managed_pages * PAGE_SIZE / (1024 * 1024));
            shell_write(" MiB), free: ");
            shell_write_uint(stats.free_pages);
            shell_write(" pages, ignored above 4 GiB: ");
            shell_write_uint(stats.ignored_pages);
            shell_write(" pages\r\n");
        } else {
            shell_write("Physical allocator unavailable\r\n");
        }
    } else if (command_is("uptime")) {
        shell_write("Uptime: ");
        shell_write_uint(timer_ticks() / TIMER_TICKS_PER_SECOND);
        shell_write(" seconds\r\n");
    } else if (command_is("disk")) {
        if (ata_pio_sector_count() == 0) {
            shell_write("No primary-master ATA disk detected\r\n");
        } else {
            shell_write("Primary-master ATA sectors: ");
            shell_write_uint(ata_pio_sector_count());
            shell_write(storage_available ?
                "; persistent filesystem ready\r\n" :
                "; persistent filesystem unavailable\r\n");
        }
    } else if (command_is("pointer")) {
        if (!pointer_available) {
            shell_write("PS/2 pointer unavailable\r\n");
        } else {
            shell_write("PS/2 pointer packets: ");
            shell_write_uint(mouse_packet_count());
            shell_write("; position: ");
            shell_write_uint(pointer_x);
            shell_write(",");
            shell_write_uint(pointer_y);
            shell_write("; buttons: ");
            shell_write_uint(pointer_buttons);
            shell_write("\r\n");
        }
    } else if (command_is("pagealloc")) {
        if (!allocator_available) {
            shell_write("pagealloc: physical allocator unavailable\r\n");
        } else if (shell_has_allocated_page) {
            shell_write("pagealloc: free the previous page first\r\n");
        } else if (!page_alloc(&shell_allocated_page)) {
            shell_write("pagealloc: out of memory\r\n");
        } else {
            shell_has_allocated_page = 1;
            shell_write("Allocated physical page at 0x");
            shell_write_hex(shell_allocated_page);
            shell_write("\r\n");
        }
    } else if (command_is("pagefree")) {
        if (!shell_has_allocated_page ||
            !page_free(shell_allocated_page)) {
            shell_write("pagefree: no allocated test page\r\n");
        } else {
            shell_has_allocated_page = 0;
            shell_write("Freed physical page\r\n");
        }
    } else if (command_is("fault")) {
        shell_write("Triggering invalid-opcode exception...\r\n");
        __asm__ volatile ("ud2");
    } else if (command_is("faultpf")) {
        shell_write("Triggering page fault...\r\n");
        const volatile uint64_t *invalid_address =
            (const volatile uint64_t *)0x4000000000ULL;
        const uint64_t value = *invalid_address;
        (void)value;
    } else if (command_is("ver")) {
        shell_write("DevOS developer preview, x86-64\r\n");
    } else if (command_is("clear")) {
        framebuffer_terminal_clear();
    } else if (command_is("ls")) {
        char *path = take_token(&arguments_end);
        shell_list(path != NULL ? path : "/");
    } else if (command_is("mkdir")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !fs_mkdir(path)) {
            shell_write("mkdir: cannot create directory\r\n");
        } else {
            persist_filesystem_change();
        }
    } else if (command_is("touch")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !fs_create(path)) {
            shell_write("touch: cannot create file\r\n");
        } else {
            persist_filesystem_change();
        }
    } else if (command_is("write")) {
        char *path = take_token(&arguments_end);
        char *text = skip_spaces(arguments_end);
        if (path == NULL || *text == '\0' ||
            !fs_write(path, text, text_length(text))) {
            shell_write("write: expected an existing file and text\r\n");
        } else {
            persist_filesystem_change();
        }
    } else if (command_is("cat")) {
        char *path = take_token(&arguments_end);
        if (path == NULL) {
            shell_write("cat: expected a path\r\n");
        } else {
            shell_cat(path);
        }
    } else if (command_is("elfcheck")) {
        shell_elfcheck(take_token(&arguments_end));
    } else if (command_is("rm")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !fs_remove(path)) {
            shell_write("rm: cannot remove path\r\n");
        } else {
            persist_filesystem_change();
        }
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
        struct mouse_event pointer_event;
        const int have_key = keyboard_get_key(&key);
        const int have_serial_character = serial_get_char(&serial_character);
        const int have_pointer_event = mouse_get_event(&pointer_event);
        if (!have_key && !have_serial_character && !have_pointer_event) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        __asm__ volatile ("sti");
        framebuffer_cursor_hide();

        if (have_pointer_event && framebuffer_width() != 0 &&
            framebuffer_height() != 0) {
            pointer_apply_event(&pointer_event);
        }

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
                framebuffer_cursor_show();
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
        framebuffer_cursor_show();
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
    if (memmap_request.response != NULL && hhdm_request.response != NULL &&
        memory_init(memmap_request.response, hhdm_request.response->offset)) {
        allocator_available = 1;
        struct memory_stats stats;
        if (memory_get_stats(&stats)) {
            serial_write("Physical allocator ready: ");
            serial_write_uint(stats.free_pages);
            serial_write(" free pages; allocator manages RAM below 4 GiB.\r\n");
        }
    } else {
        serial_write("Physical allocator unavailable: Limine memory map/HHDM invalid.\r\n");
    }

    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (response != NULL && response->framebuffers != NULL &&
        response->framebuffer_count > 0 &&
        framebuffer_init(response->framebuffers[0])) {
        desktop_draw(detected_memory_mib);
        serial_write("Framebuffer desktop initialized.\r\n");
    } else {
        serial_write("Framebuffer unavailable; using serial console.\r\n");
    }

    pointer_x = framebuffer_width() / 2;
    pointer_y = framebuffer_height() / 2;
    fs_init();
    interrupts_init();
    pointer_available = mouse_is_available();
    if (framebuffer_width() != 0 && framebuffer_height() != 0) {
        framebuffer_cursor_set(pointer_x, pointer_y);
        framebuffer_cursor_show();
    }
    pci_init();
    serial_write("PCI enumeration complete: ");
    serial_write_uint(pci_device_count());
    serial_write("; IDE controllers: ");
    serial_write_uint(pci_ide_controller_count());
    serial_write("\r\n");
    if (ata_pio_init()) {
        serial_write("ATA primary master ready: ");
        serial_write_uint(ata_pio_sector_count());
        serial_write(" sectors\r\n");
        const int load_result = fs_disk_load();
        if (load_result == FS_DISK_LOAD_OK) {
            storage_available = 1;
            serial_write("Persistent filesystem loaded.\r\n");
        } else if (load_result == FS_DISK_LOAD_EMPTY) {
            storage_available = 1;
            serial_write("Persistent filesystem blank; changes will be saved.\r\n");
        } else {
            storage_available = 0;
            serial_write("Persistent filesystem unavailable; disk was not changed.\r\n");
        }
    } else {
        serial_write("ATA primary master not present or unsupported.\r\n");
    }
    kernel_shell();
}
