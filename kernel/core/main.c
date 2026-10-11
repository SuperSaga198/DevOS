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
#include "system.h"
#include "file_manager.h"
#include "setup_wizard.h"
#include "scheduler.h"
#include "process.h"
#include "text_editor.h"
#include "pc_speaker.h"
#include "e1000.h"

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
static int network_available;
static int pointer_available;
static uint64_t pointer_x;
static uint64_t pointer_y;
static uint8_t pointer_buttons;
static uint8_t gateway_reachable;

static void shell_write(const char *text)
{
    serial_write(text);
    framebuffer_terminal_write(text);
}

static void desktop_refresh_draw(void)
{
    struct desktop_runtime_status status = {
        .network_ready = (uint8_t)network_available,
        .gateway_reachable = gateway_reachable,
        .storage_ready = (uint8_t)storage_available,
        .audio_ready = 1,
        .udp_echoes = network_available ? e1000_udp_echoes() : 0,
        .tcp_echoes = network_available ? e1000_tcp_echoes() : 0,
        .tcp_segments = network_available ? e1000_tcp_segments() : 0,
    };
    struct memory_stats memory;
    if (memory_get_stats(&memory)) {
        status.managed_pages = memory.managed_pages;
        status.free_pages = memory.free_pages;
    }
    desktop_set_runtime_status(&status);
    desktop_draw(detected_memory_mib);
}

static void persist_filesystem_change(void)
{
    if (storage_available && !fs_disk_save()) {
        shell_write("warning: change is only in RAM; disk save failed\r\n");
    }
}

static void service_monitor_task(void *argument)
{
    (void)argument;
    serial_write("Service-monitor kernel task executing.\r\n");
    uint64_t next_update = timer_ticks();
    for (;;) {
        const uint64_t now = timer_ticks();
        if (now >= next_update) {
            (void)devos_service_set_state(
                DEVOS_SERVICE_STORAGE,
                storage_available ? DEVOS_SERVICE_READY : DEVOS_SERVICE_OFF);
            (void)devos_service_set_state(
                DEVOS_SERVICE_NETWORK,
                network_available ? DEVOS_SERVICE_READY : DEVOS_SERVICE_OFF);
            (void)devos_service_set_state(
                DEVOS_SERVICE_AUDIO, DEVOS_SERVICE_READY);
            (void)devos_service_set_state(
                DEVOS_SERVICE_DESKTOP,
                framebuffer_width() != 0 && framebuffer_height() != 0
                    ? DEVOS_SERVICE_READY : DEVOS_SERVICE_OFF);
            next_update = now + TIMER_TICKS_PER_SECOND;
        }
        __asm__ volatile ("sti; hlt");
    }
}

static void network_poll_task(void *argument)
{
    (void)argument;
    for (;;) {
        e1000_poll();
        __asm__ volatile ("sti; hlt");
    }
}

extern const uint8_t user_app_image_start[];
extern const uint8_t user_app_image_end[];

static int launch_user_demo(void)
{
    const size_t image_size = (size_t)(
        (uintptr_t)user_app_image_end - (uintptr_t)user_app_image_start);
    return process_exec_elf("demo", user_app_image_start,
                            image_size);
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

static int parse_decimal(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return 0;
    }
    uint32_t parsed = 0;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }
        const uint32_t digit = (uint32_t)(text[index] - '0');
        if (parsed > (UINT32_MAX - digit) / 10) {
            return 0;
        }
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return 1;
}

static void desktop_file_manager_refresh(void)
{
    struct desktop_file_item visible_items[DESKTOP_FILE_ITEMS_MAX];
    const size_t count = file_manager_entry_count();
    for (size_t index = 0; index < count; ++index) {
        const struct file_manager_entry *entry =
            file_manager_entry_at(index);
        if (entry == NULL) {
            continue;
        }
        size_t name_index = 0;
        while (entry->name[name_index] != '\0' &&
               name_index + 1 < sizeof(visible_items[index].name)) {
            visible_items[index].name[name_index] = entry->name[name_index];
            ++name_index;
        }
        visible_items[index].name[name_index] = '\0';
        visible_items[index].is_directory = entry->is_directory;
        visible_items[index].size = entry->size;
    }

    (void)desktop_file_manager_update(file_manager_path(),
                                      count != 0 ? visible_items : NULL,
                                      count, file_manager_preview());
}

static void file_manager_activate(int action)
{
    if (action == DESKTOP_ACTION_FILE_PARENT) {
        file_manager_parent();
        desktop_file_manager_refresh();
        return;
    }

    const int item_index = action - DESKTOP_ACTION_FILE_ITEM_BASE;
    if (item_index < 0 ||
        !file_manager_open_entry((size_t)item_index)) {
        return;
    }
    desktop_file_manager_refresh();
}

static void setup_render(const char *message)
{
    const enum setup_wizard_state state = setup_wizard_get_state();
    const char *input = state == SETUP_WIZARD_USERNAME ||
                                state == SETUP_WIZARD_HOSTNAME
                            ? setup_wizard_get_input()
                            : state == SETUP_WIZARD_COMMIT
                                  ? "Preparing your profile" : "";
    framebuffer_setup_draw((uint8_t)state, input, message);
}

static void setup_try_commit(void)
{
    int saved_to_disk = 0;
    if (storage_available) {
        if (!fs_disk_save()) {
            setup_wizard_persist_result(0);
            setup_render("Disk save failed. Press Enter to retry.");
            serial_write("OOBE save failed; press Enter to retry.\r\n");
            return;
        }
        saved_to_disk = 1;
    }

    setup_wizard_persist_result(1);
    const struct setup_profile *profile = setup_wizard_get_profile();
    desktop_set_identity(profile->username, profile->hostname);
    if (saved_to_disk) {
        serial_write("OOBE setup saved to disk.\r\n");
    } else {
        serial_write("OOBE setup complete; no persistent disk, profile is RAM-only.\r\n");
    }
}

static void first_run_setup(void)
{
    setup_wizard_init();
    if (setup_wizard_get_state() == SETUP_WIZARD_COMPLETE) {
        const struct setup_profile *profile = setup_wizard_get_profile();
        desktop_set_identity(profile->username, profile->hostname);
        serial_write("OOBE profile loaded: ");
        serial_write(profile->username);
        serial_write("@");
        serial_write(profile->hostname);
        serial_write("\r\n");
        return;
    }

    serial_write("OOBE required. Enter a user name, then a computer name.\r\n");
    setup_render(NULL);
    while (setup_wizard_get_state() != SETUP_WIZARD_COMPLETE) {
        __asm__ volatile ("cli");
        uint16_t key = 0;
        uint8_t serial_character = 0;
        const int have_key = keyboard_get_key(&key);
        const int have_serial = serial_get_char(&serial_character);
        if (!have_key && !have_serial) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        __asm__ volatile ("sti");

        if (have_key) {
            setup_wizard_input(key);
        }
        if (have_serial) {
            const uint8_t character = serial_character;
            if (character == '\n' && last_serial_was_cr) {
                last_serial_was_cr = 0;
            } else {
                last_serial_was_cr = character == '\r';
                setup_wizard_input(character);
            }
        }

        if (setup_wizard_get_state() == SETUP_WIZARD_COMMIT) {
            setup_try_commit();
        } else if (setup_wizard_get_state() == SETUP_WIZARD_USERNAME) {
            setup_render(NULL);
        } else if (setup_wizard_get_state() == SETUP_WIZARD_HOSTNAME) {
            setup_render(NULL);
        } else if (setup_wizard_get_state() == SETUP_WIZARD_SAVE_ERROR) {
            setup_render("Disk save failed. Press Enter to retry.");
        }
    }
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

static void shell_open_terminal(void)
{
    terminal_open = 1;
    command_length = 0;
    framebuffer_terminal_open();
    shell_write("DevOS terminal. Type help for commands.\r\n");
    shell_prompt();
}

static void editor_render(const char *status)
{
    framebuffer_terminal_clear();
    framebuffer_terminal_write("DEVOS EDITOR: ");
    framebuffer_terminal_write(text_editor_path());
    framebuffer_terminal_write(
        "\r\nCtrl+S or :w save | Ctrl+Q or :q discard and exit\r\n");
    framebuffer_terminal_write("----------------------------------------\r\n");

    const char *contents = text_editor_contents();
    const size_t size = text_editor_size();
    for (size_t index = 0; index < size; ++index) {
        const unsigned char value = (unsigned char)contents[index];
        char character[2] = {
            value == '\n' ? '\n' :
                value >= 0x20 && value <= 0x7e ? (char)value : '?',
            '\0',
        };
        framebuffer_terminal_write(character);
    }
    framebuffer_terminal_write("\r\n----------------------------------------\r\n");
    framebuffer_terminal_write("Changes are unsaved until saved");
    if (status != NULL) {
        framebuffer_terminal_write(" - ");
        framebuffer_terminal_write(status);
    }
    framebuffer_terminal_write("\r\n");
}

static void editor_echo_input(uint16_t key)
{
    if (key == KEYBOARD_KEY_ENTER || key == '\r' || key == '\n') {
        framebuffer_terminal_write("\r\n");
        serial_write("\r\n");
    } else if (key == KEYBOARD_KEY_BACKSPACE || key == '\b' || key == 0x7f) {
        framebuffer_terminal_backspace();
        serial_write("\b \b");
    } else if (key >= 0x20 && key <= 0x7e) {
        const char character[2] = { (char)key, '\0' };
        framebuffer_terminal_write(character);
        serial_write(character);
    }
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
        shell_write("  help     - show this help\r\n");
        shell_write("  mem      - show usable memory\r\n");
        shell_write("  ver      - show kernel version\r\n");
        shell_write("  clear    - clear the terminal\r\n");
        shell_write("  home     - return to the desktop\r\n");
        shell_write("  apps     - list open desktop windows\r\n");
        shell_write("  start APP - open a desktop app window\r\n");
        shell_write("  focus APP - focus an open desktop app\r\n");
        shell_write("  close APP - close a desktop app window\r\n");
        shell_write("  run demo  - load and run the built-in ELF user app\r\n");
        shell_write("  ps       - list scheduled kernel tasks\r\n");
        shell_write("  sysinfo  - show system service readiness\r\n");
        shell_write("  net      - show network service status\r\n");
        shell_write("  ping     - send an ICMP echo to the QEMU gateway\r\n");
        shell_write("  udp      - show UDP echo service status (port 7007)\r\n");
        shell_write("  tcp      - show TCP echo service status (port 7008)\r\n");
        shell_write("  audio    - show audio service status\r\n");
        shell_write("  beep [HZ [MS]] - play a PC speaker tone\r\n");
        shell_write("  ls [PATH]       - list a RAM filesystem directory\r\n");
        shell_write("  mkdir PATH      - create a directory\r\n");
        shell_write("  touch PATH      - create an empty file\r\n");
        shell_write("  write PATH TEXT - replace a file's contents\r\n");
        shell_write("  cat PATH        - print a file\r\n");
        shell_write("  edit PATH       - edit a file (Ctrl+S save, Ctrl+Q discard)\r\n");
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
    } else if (command_is("apps")) {
        if (!desktop_window_count()) {
            shell_write("No open desktop windows\r\n");
        } else {
            shell_write("Open windows:\r\n");
            for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
                const char *window_name = desktop_window_name(index);
                if (window_name != NULL) {
                    shell_write("  ");
                    shell_write(window_name);
                    shell_write("\r\n");
                }
            }
        }
    } else if (command_is("start")) {
        char *app = take_token(&arguments_end);
        if (app == NULL) {
            shell_write("start: expected a window name\r\n");
        } else {
            if (desktop_window_open(app, 120 + desktop_window_count() * 25,
                                    90 + desktop_window_count() * 20,
                                    260, 170,
                                    0x2a74d1)) {
                shell_write("Started desktop window: ");
                shell_write(app);
                shell_write("\r\n");
                desktop_refresh_draw();
            } else {
                shell_write("start: unable to open ");
                shell_write(app);
                shell_write("\r\n");
            }
        }
    } else if (command_is("run")) {
        char *app = take_token(&arguments_end);
        if (app == NULL) {
            shell_write("run: expected the app name 'demo'\r\n");
        } else {
            size_t app_name_index = 0;
            static const char expected_app[] = "demo";
            while (app[app_name_index] != '\0' &&
                   expected_app[app_name_index] != '\0') {
                char actual = app[app_name_index];
                if (actual >= 'A' && actual <= 'Z') {
                    actual = (char)(actual - 'A' + 'a');
                }
                if (actual != expected_app[app_name_index]) {
                    break;
                }
                ++app_name_index;
            }
            if (app[app_name_index] != '\0' ||
                expected_app[app_name_index] != '\0') {
                shell_write("run: unknown built-in application\r\n");
            } else if (!launch_user_demo()) {
                shell_write("run: could not load or schedule the ELF app\r\n");
            } else {
                shell_write("ELF app scheduled in isolated user mode\r\n");
            }
        }
    } else if (command_is("focus")) {
        char *app = take_token(&arguments_end);
        if (app == NULL) {
            shell_write("focus: expected a window name\r\n");
        } else if (!desktop_window_is_open(app)) {
            shell_write("focus: window is not open\r\n");
        } else {
            desktop_window_focus(app);
            desktop_refresh_draw();
            shell_write("Focused window: ");
            shell_write(app);
            shell_write("\r\n");
        }
    } else if (command_is("close")) {
        char *app = take_token(&arguments_end);
        if (app == NULL) {
            shell_write("close: expected a window name\r\n");
        } else if (!desktop_window_close(app)) {
            shell_write("close: window not found\r\n");
        } else {
            desktop_refresh_draw();
            shell_write("Closed window: ");
            shell_write(app);
            shell_write("\r\n");
        }
    } else if (command_is("ps")) {
        struct scheduler_task_info tasks[SCHEDULER_MAX_TASKS];
        const size_t count =
            scheduler_task_snapshot(tasks, SCHEDULER_MAX_TASKS);
        shell_write("STATE    TICKS  TYPE    SCHEDULED TASK\r\n");
        if (count != 0) {
            for (size_t index = 0; index < count; ++index) {
                shell_write(tasks[index].state == SCHEDULER_TASK_RUNNING
                                ? "running  " :
                            tasks[index].state == SCHEDULER_TASK_READY
                                ? "ready    " : "exited   ");
                shell_write_uint(tasks[index].ticks);
                shell_write("  ");
                shell_write(tasks[index].name);
                shell_write(tasks[index].is_user ? "  ring-3" : "  ring-0");
                shell_write("\r\n");
            }
        } else {
            shell_write("No scheduled kernel tasks\r\n");
        }
    } else if (command_is("sysinfo")) {
        struct devos_service_status status;
        if (devos_service_status(&status)) {
            shell_write("Component registry: ");
            shell_write(devos_service_state_name(status.component_registry_ready));
            shell_write("\r\nStorage: ");
            shell_write(devos_service_state_name(status.storage_ready));
            shell_write("\r\nNetworking: ");
            shell_write(devos_service_state_name(status.network_ready));
            shell_write("\r\nAudio: ");
            shell_write(devos_service_state_name(status.audio_ready));
            shell_write("\r\nDesktop: ");
            shell_write(devos_service_state_name(status.desktop_ready));
            shell_write("\r\n");
        }
    } else if (command_is("net")) {
        if (!network_available) {
            shell_write("Network: no supported e1000 NIC detected\r\n");
        } else {
            const uint8_t *address = e1000_mac_address();
            shell_write("e1000 ready; static IPv4 10.0.2.15; MAC ");
            for (size_t index = 0; index < 6; ++index) {
                shell_write_hex(address[index]);
                if (index != 5) {
                    shell_write(":");
                }
            }
            shell_write("; RX frames ");
            shell_write_uint(e1000_received_frames());
            shell_write("; ICMP replies ");
            shell_write_uint(e1000_echo_replies());
            shell_write("; UDP echo replies ");
            shell_write_uint(e1000_udp_echoes());
            shell_write("; UDP echo port 7007");
            shell_write("; UDP RX ");
            shell_write_uint(e1000_udp_datagrams());
            shell_write("; UDP rejected ");
            shell_write_uint(e1000_udp_rejected());
            shell_write("; TCP echo replies ");
            shell_write_uint(e1000_tcp_echoes());
            shell_write("; TCP RX ");
            shell_write_uint(e1000_tcp_segments());
            shell_write("; TCP echo port 7008");
            shell_write("\r\n");
        }
    } else if (command_is("ping")) {
        if (!network_available) {
            shell_write("ping: no supported e1000 NIC detected\r\n");
        } else if (e1000_ping_gateway()) {
            gateway_reachable = 1;
            shell_write("10.0.2.2 replied to ICMP echo\r\n");
        } else {
            gateway_reachable = 0;
            shell_write("ping: no ICMP reply from 10.0.2.2\r\n");
        }
    } else if (command_is("udp")) {
        if (!network_available) {
            shell_write("udp: no supported e1000 NIC detected\r\n");
        } else {
            shell_write("UDP echo service: IPv4 10.0.2.15 port 7007; replies ");
            shell_write_uint(e1000_udp_echoes());
            shell_write(" of ");
            shell_write_uint(e1000_udp_datagrams());
            shell_write(" datagrams; rejected ");
            shell_write_uint(e1000_udp_rejected());
            shell_write("\r\n");
        }
    } else if (command_is("tcp")) {
        if (!network_available) {
            shell_write("tcp: no supported e1000 NIC detected\r\n");
        } else {
            shell_write("TCP echo service: IPv4 10.0.2.15 port 7008; replies ");
            shell_write_uint(e1000_tcp_echoes());
            shell_write(" of ");
            shell_write_uint(e1000_tcp_segments());
            shell_write(" received segments\r\n");
        }
    } else if (command_is("audio")) {
        shell_write("Audio: PC speaker PIT tone output; no PCM/mixer backend\r\n");
    } else if (command_is("beep")) {
        char *frequency_text = take_token(&arguments_end);
        char *duration_text = take_token(&arguments_end);
        uint32_t frequency = 880;
        uint32_t duration = 250;
        if ((frequency_text != NULL &&
             !parse_decimal(frequency_text, &frequency)) ||
            (duration_text != NULL &&
             !parse_decimal(duration_text, &duration))) {
            shell_write("beep: use decimal frequency and duration\r\n");
        } else if (!pc_speaker_play_tone(frequency, duration)) {
            shell_write("beep: valid range is 20-20000 Hz and 1-2000 ms\r\n");
        } else {
            shell_write("PC speaker tone played\r\n");
        }
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
            file_manager_refresh();
            desktop_file_manager_refresh();
            persist_filesystem_change();
        }
    } else if (command_is("touch")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !fs_create(path)) {
            shell_write("touch: cannot create file\r\n");
        } else {
            file_manager_refresh();
            desktop_file_manager_refresh();
            persist_filesystem_change();
        }
    } else if (command_is("write")) {
        char *path = take_token(&arguments_end);
        char *text = skip_spaces(arguments_end);
        if (path == NULL || *text == '\0' ||
            !fs_write(path, text, text_length(text))) {
            shell_write("write: expected an existing file and text\r\n");
        } else {
            file_manager_refresh();
            desktop_file_manager_refresh();
            persist_filesystem_change();
        }
    } else if (command_is("cat")) {
        char *path = take_token(&arguments_end);
        if (path == NULL) {
            shell_write("cat: expected a path\r\n");
        } else {
            shell_cat(path);
        }
    } else if (command_is("edit")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !text_editor_open(path)) {
            shell_write("edit: expected a valid file path\r\n");
        } else {
            serial_write("Text editor open: ");
            serial_write(text_editor_path());
            serial_write("\r\n");
            editor_render(NULL);
            return;
        }
    } else if (command_is("elfcheck")) {
        shell_elfcheck(take_token(&arguments_end));
    } else if (command_is("rm")) {
        char *path = take_token(&arguments_end);
        if (path == NULL || !fs_remove(path)) {
            shell_write("rm: cannot remove path\r\n");
        } else {
            file_manager_refresh();
            desktop_file_manager_refresh();
            persist_filesystem_change();
        }
    } else if (command_is("home")) {
        terminal_open = 0;
        desktop_refresh_draw();
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
    if (text_editor_is_open()) {
        const enum text_editor_result result = text_editor_input(key);
        if (result == TEXT_EDITOR_CHANGED) {
            editor_echo_input(key);
        } else if (result == TEXT_EDITOR_SAVED) {
            if (key == KEYBOARD_KEY_ENTER || key == '\r' || key == '\n') {
                editor_echo_input(key);
            }
            serial_write("Text editor saved.\r\n");
            framebuffer_terminal_write("\r\nSaved.\r\n");
            file_manager_refresh();
            desktop_file_manager_refresh();
            persist_filesystem_change();
        } else if (result == TEXT_EDITOR_CLOSED) {
            framebuffer_terminal_clear();
            shell_write("Editor closed.\r\n");
            shell_prompt();
        } else if (result == TEXT_EDITOR_ERROR) {
            framebuffer_terminal_write("\r\nEditor limit reached or save failed.\r\n");
        }
        return;
    }

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
            if (desktop_animation_step()) {
                desktop_refresh_draw();
            }
            continue;
        }
        __asm__ volatile ("sti");
        framebuffer_cursor_hide();

        if (have_pointer_event && framebuffer_width() != 0 &&
            framebuffer_height() != 0) {
            pointer_apply_event(&pointer_event);
            if (!terminal_open) {
                const int desktop_action = desktop_pointer_event(
                    pointer_x, pointer_y, pointer_event.buttons);
                if (desktop_action == DESKTOP_ACTION_OPEN_TERMINAL) {
                    shell_open_terminal();
                } else if (desktop_action == DESKTOP_ACTION_FILE_PARENT ||
                           desktop_action >= DESKTOP_ACTION_FILE_ITEM_BASE) {
                    file_manager_activate(desktop_action);
                    desktop_refresh_draw();
                } else if (desktop_action == DESKTOP_ACTION_AUDIO_TEST_TONE) {
                    if (!pc_speaker_play_tone(880, 180)) {
                        serial_write("Desktop audio test tone failed.\r\n");
                    }
                    desktop_refresh_draw();
                } else if (desktop_action == DESKTOP_ACTION_NETWORK_PING) {
                    gateway_reachable = e1000_ping_gateway() != 0;
                    if (gateway_reachable) {
                        serial_write("Desktop network panel: gateway replied.\r\n");
                    } else {
                        serial_write("Desktop network panel: gateway ping failed.\r\n");
                    }
                    desktop_refresh_draw();
                } else if (desktop_action == DESKTOP_ACTION_REDRAW ||
                           desktop_action == DESKTOP_ACTION_ANIMATION_TOGGLE ||
                           (desktop_action >= DESKTOP_ACTION_WALLPAPER_BASE &&
                            desktop_action < DESKTOP_ACTION_WALLPAPER_BASE + 4)) {
                    desktop_refresh_draw();
                }
            }
        }

        if (have_key) {
            if (!terminal_open) {
                if (key == 't' || key == 'T') {
                    shell_open_terminal();
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
                shell_open_terminal();
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
        desktop_refresh_draw();
        serial_write("Framebuffer desktop initialized.\r\n");
    } else {
        serial_write("Framebuffer unavailable; using serial console.\r\n");
    }

    pointer_x = framebuffer_width() / 2;
    pointer_y = framebuffer_height() / 2;
    devos_system_init();
    if (framebuffer_width() != 0 && framebuffer_height() != 0) {
        (void)devos_service_set_state(DEVOS_SERVICE_DESKTOP,
                                      DEVOS_SERVICE_READY);
    }
    fs_init();
    file_manager_init();
    desktop_file_manager_refresh();
    interrupts_init();
    pc_speaker_init();
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
    if (e1000_init()) {
        network_available = 1;
        serial_write("Intel e1000 NIC ready at 10.0.2.15.\r\n");
    } else {
        serial_write("No supported Intel e1000 NIC; network remains offline.\r\n");
    }
    if (ata_pio_init()) {
        serial_write("ATA primary master ready: ");
        serial_write_uint(ata_pio_sector_count());
        serial_write(" sectors\r\n");
        const int load_result = fs_disk_load();
        if (load_result == FS_DISK_LOAD_OK) {
            storage_available = 1;
            (void)devos_service_set_state(DEVOS_SERVICE_STORAGE,
                                          DEVOS_SERVICE_READY);
            serial_write("Persistent filesystem loaded.\r\n");
        } else if (load_result == FS_DISK_LOAD_EMPTY) {
            storage_available = 1;
            (void)devos_service_set_state(DEVOS_SERVICE_STORAGE,
                                          DEVOS_SERVICE_READY);
            serial_write("Persistent filesystem blank; changes will be saved.\r\n");
        } else {
            storage_available = 0;
            serial_write("Persistent filesystem unavailable; disk was not changed.\r\n");
        }
    } else {
        serial_write("ATA primary master not present or unsupported.\r\n");
    }
    file_manager_refresh();
    desktop_file_manager_refresh();
    first_run_setup();
    if (framebuffer_width() != 0 && framebuffer_height() != 0) {
        desktop_refresh_draw();
    }
    __asm__ volatile ("cli");
    scheduler_init();
    uint64_t kernel_address_space;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(kernel_address_space));
    scheduler_set_kernel_address_space(kernel_address_space);
    const int network_task_started =
        network_available &&
        scheduler_create("network-rx", network_poll_task, NULL);
    const int monitor_started =
        scheduler_create("service-monitor", service_monitor_task, NULL);
    const int user_demo_started =
        allocator_available && launch_user_demo();
    __asm__ volatile ("sti");
    if (!monitor_started) {
        serial_write("Kernel service monitor failed to start.\r\n");
    } else {
        serial_write("Preemptive kernel-task scheduler ready; "
                     "service monitor started.\r\n");
    }
    if (network_available && !network_task_started) {
        serial_write("Network receive task failed to start.\r\n");
    }
    if (!user_demo_started) {
        serial_write("Ring-3 demo process could not be created.\r\n");
    } else {
        serial_write("Ring-3 user-process isolation initialized.\r\n");
    }
    kernel_shell();
}
