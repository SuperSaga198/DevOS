#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct limine_framebuffer *active_framebuffer;
static uint8_t terminal_active;
static uint16_t terminal_columns;
static uint16_t terminal_rows;
static uint16_t terminal_column;
static uint16_t terminal_row;
static char terminal_cells[80][160];
#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 16
static uint32_t cursor_background[CURSOR_HEIGHT][CURSOR_WIDTH];
static uint64_t cursor_x;
static uint64_t cursor_y;
static uint8_t cursor_visible;
static uint8_t cursor_background_width;
static uint8_t cursor_background_height;

#define DESKTOP_WINDOW_MAX 8u

struct desktop_window_slot {
    uint8_t open;
    uint8_t focused;
    char name[16];
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
    uint32_t accent;
};

static struct desktop_window_slot desktop_windows[DESKTOP_WINDOW_MAX];
static uint8_t desktop_initialized;
static uint8_t desktop_start_menu_open;
static uint8_t desktop_previous_buttons;
static int desktop_drag_slot = -1;
static uint64_t desktop_drag_offset_x;
static uint64_t desktop_drag_offset_y;
static char desktop_file_path[DESKTOP_FILE_PATH_MAX];
static struct desktop_file_item desktop_file_items[DESKTOP_FILE_ITEMS_MAX];
static size_t desktop_file_item_count;
static char desktop_file_preview[DESKTOP_FILE_PREVIEW_MAX];
static char desktop_username[32] = "developer";
static char desktop_hostname[32] = "devos";
static struct desktop_runtime_status desktop_status;

#define DESKTOP_SURFACE 0xf7f8fcu
#define DESKTOP_TEXT 0x172033u
#define DESKTOP_MUTED 0x68758bu
#define DESKTOP_ACCENT 0x6c8cffu
#define DESKTOP_TASKBAR 0x111827u
#define DESKTOP_ANIMATION_STEPS 8u

struct desktop_wallpaper {
    const char *name;
    uint32_t top;
    uint32_t bottom;
    uint32_t motif;
    uint32_t motif_light;
    uint32_t accent;
};

static const struct desktop_wallpaper desktop_wallpapers[] = {
    { "DEEP BLUE", 0x101827u, 0x1e3a5fu, 0x203e63u, 0x24466cu, 0x6c8cffu },
    { "AURORA", 0x102422u, 0x176b69u, 0x1b605bu, 0x21827au, 0x67d6b4u },
    { "SUNSET", 0x281b31u, 0x703e56u, 0x633e66u, 0x81506fu, 0xff9b73u },
    { "GRAPHITE", 0x171a21u, 0x424958u, 0x363e4cu, 0x4d5767u, 0xa9b8d0u },
};
#define DESKTOP_WALLPAPER_COUNT \
    (sizeof(desktop_wallpapers) / sizeof(desktop_wallpapers[0]))

enum desktop_animation_kind {
    DESKTOP_ANIMATION_NONE,
    DESKTOP_ANIMATION_MENU_OPEN,
    DESKTOP_ANIMATION_MENU_CLOSE,
    DESKTOP_ANIMATION_WINDOW_OPEN,
    DESKTOP_ANIMATION_WINDOW_CLOSE,
};

static enum desktop_animation_kind desktop_animation;
static uint8_t desktop_animation_frame;
static size_t desktop_animation_window;
static uint8_t desktop_menu_visible;
static uint8_t desktop_has_drawn;
static struct desktop_window_slot desktop_closing_window;
static uint8_t desktop_closing_window_visible;
static size_t desktop_selected_wallpaper;
static uint8_t desktop_motion_enabled = 1;

static void desktop_animation_start(enum desktop_animation_kind kind)
{
    if (!desktop_motion_enabled) {
        desktop_animation = DESKTOP_ANIMATION_NONE;
        desktop_animation_frame = 0;
        if (kind == DESKTOP_ANIMATION_MENU_CLOSE) {
            desktop_menu_visible = 0;
        } else if (kind == DESKTOP_ANIMATION_WINDOW_CLOSE) {
            desktop_closing_window_visible = 0;
        }
        return;
    }
    desktop_animation = kind;
    desktop_animation_frame = 0;
}

size_t desktop_wallpaper_index(void)
{
    return desktop_selected_wallpaper;
}

int desktop_animations_enabled(void)
{
    return desktop_motion_enabled != 0;
}

static const char *desktop_window_lookup_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    while (*name == ' ' || *name == '\t') {
        ++name;
    }
    return *name == '\0' ? NULL : name;
}

static void desktop_format_uint(char *output, size_t capacity, uint64_t value)
{
    if (output == NULL || capacity == 0) {
        return;
    }
    char digits[24];
    size_t length = 0;
    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && length < sizeof(digits));

    size_t output_index = 0;
    while (length != 0 && output_index + 1 < capacity) {
        output[output_index++] = digits[--length];
    }
    output[output_index] = '\0';
}

static char desktop_upper_char(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - ('a' - 'A'));
    }
    return value;
}

static int desktop_window_name_matches(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (*left != '\0' && *right != '\0') {
        if (desktop_upper_char(*left) != desktop_upper_char(*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static void desktop_stash_name(char *buffer, size_t length, const char *name)
{
    if (buffer == NULL || length == 0 || name == NULL) {
        return;
    }
    size_t index = 0;
    while (name[index] != '\0' && index + 1 < length) {
        buffer[index] = desktop_upper_char(name[index]);
        ++index;
    }
    buffer[index] = '\0';
}

static size_t desktop_name_length(const char *name)
{
    size_t length = 0;
    while (name != NULL && name[length] != '\0') {
        ++length;
    }
    return length;
}

static void desktop_copy_string(char *destination, size_t capacity,
                                const char *source)
{
    if (destination == NULL || capacity == 0) {
        return;
    }
    size_t index = 0;
    if (source != NULL) {
        while (source[index] != '\0' && index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

void desktop_set_identity(const char *username, const char *hostname)
{
    desktop_copy_string(desktop_username, sizeof(desktop_username), username);
    desktop_copy_string(desktop_hostname, sizeof(desktop_hostname), hostname);
}

void desktop_set_runtime_status(
    const struct desktop_runtime_status *status)
{
    if (status != NULL) {
        desktop_status = *status;
    }
}

void framebuffer_setup_draw(uint8_t state, const char *input,
                            const char *message)
{
    if (active_framebuffer == NULL) {
        return;
    }

    const uint64_t width = active_framebuffer->width;
    const uint64_t height = active_framebuffer->height;
    const uint64_t panel_width = width > 680 ? 560 :
                                 width > 32 ? width - 32 : width;
    const uint64_t panel_x = (width - panel_width) / 2;
    const uint64_t panel_y = height > 460 ? 110 : 24;
    framebuffer_clear(0x162338);
    framebuffer_rect(panel_x, panel_y, panel_width,
                     height > panel_y + 80 ? height - panel_y - 60 : 220,
                     0xf4f6fa);
    framebuffer_rect(panel_x, panel_y, panel_width, 56, 0x1e4779);
    framebuffer_text(panel_x + 24, panel_y + 18, "DEVOS FIRST-RUN SETUP",
                     0xffffff, 0x1e4779, 1);

    const uint64_t content_x = panel_x + 36;
    framebuffer_text(content_x, panel_y + 92, "Welcome to DevOS",
                     0x20314a, 0xf4f6fa, 3);
    if (state == 1) {
        framebuffer_text(content_x, panel_y + 150, "Create your user name",
                         0x34445d, 0xf4f6fa, 2);
        framebuffer_text(content_x, panel_y + 194, "Letters, numbers, hyphen; max 31",
                         0x5f6d7b, 0xf4f6fa, 1);
    } else if (state == 2) {
        framebuffer_text(content_x, panel_y + 150, "Name this computer",
                         0x34445d, 0xf4f6fa, 2);
        framebuffer_text(content_x, panel_y + 194, "Letters, numbers, hyphen; max 31",
                         0x5f6d7b, 0xf4f6fa, 1);
    } else if (state == 3) {
        framebuffer_text(content_x, panel_y + 150, "Saving your setup...",
                         0x34445d, 0xf4f6fa, 2);
    } else {
        framebuffer_text(content_x, panel_y + 150, "Setup could not be saved",
                         0xa52a2a, 0xf4f6fa, 2);
    }

    framebuffer_rect(content_x, panel_y + 224, panel_width - 72, 38, 0xffffff);
    framebuffer_text(content_x + 10, panel_y + 240,
                     input != NULL ? input : "",
                     0x20314a, 0xffffff, 1);
    if (message != NULL && message[0] != '\0') {
        framebuffer_text(content_x, panel_y + 284, message,
                         0xa52a2a, 0xf4f6fa, 1);
    }
    framebuffer_text(content_x, height > 34 ? height - 30 : 0,
                     "ENTER: continue   BACKSPACE: edit",
                     0xd6e7ff, 0x162338, 1);
}

static int desktop_window_find_slot(const char *name, size_t *index)
{
    const char *safe_name = desktop_window_lookup_name(name);
    if (safe_name == NULL) {
        return 0;
    }

    for (size_t slot_index = 0; slot_index < DESKTOP_WINDOW_MAX; ++slot_index) {
        if (desktop_windows[slot_index].open &&
            desktop_window_name_matches(desktop_windows[slot_index].name, safe_name)) {
            if (index != NULL) {
                *index = slot_index;
            }
            return 1;
        }
    }
    return 0;
}

static void desktop_window_render_slot_content(struct desktop_window_slot *slot)
{
    framebuffer_rect(slot->x - 2, slot->y - 2, slot->width + 4,
                     slot->height + 4, slot->focused ? 0x6c8cff : 0x344158);
    framebuffer_rect(slot->x, slot->y, slot->width, slot->height,
                     DESKTOP_SURFACE);
    framebuffer_rect(slot->x, slot->y, slot->width, 34,
                     slot->focused ? 0xffffff : 0xe9edf5);
    framebuffer_rect(slot->x + 12, slot->y + 11, 12, 12,
                     slot->focused ? 0xe45b67 : 0xb7c0d0);
    framebuffer_rect(slot->x + 15, slot->y + 14, 6, 6, 0xffffff);
    framebuffer_text(slot->x + 34, slot->y + 12, slot->name,
                     DESKTOP_TEXT,
                     slot->focused ? 0xffffff : 0xe9edf5, 1);

    framebuffer_rect(slot->x + 1, slot->y + 35, slot->width - 2,
                     slot->height - 36, DESKTOP_SURFACE);
    if (desktop_window_name_matches(slot->name, "FILES")) {
        framebuffer_text(slot->x + 20, slot->y + 46, desktop_file_path,
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_text(slot->x + 20, slot->y + 64, "[..]",
                         DESKTOP_ACCENT, DESKTOP_SURFACE, 1);
        for (size_t index = 0; index < desktop_file_item_count; ++index) {
            const uint64_t row_y = slot->y + 82 + index * 16;
            char label[DESKTOP_FILE_NAME_MAX + 5];
            const char *prefix = desktop_file_items[index].is_directory
                                     ? "[D] " : "    ";
            size_t out = 0;
            while (prefix[out] != '\0') {
                label[out] = prefix[out];
                ++out;
            }
            size_t name_index = 0;
            while (desktop_file_items[index].name[name_index] != '\0' &&
                   out + 1 < sizeof(label)) {
                label[out++] = desktop_file_items[index].name[name_index++];
            }
            label[out] = '\0';
            framebuffer_rect(slot->x + 16, row_y - 2, slot->width - 32,
                             14, index % 2 == 0 ? 0xf0f3f9 : 0xffffff);
            framebuffer_text(slot->x + 20, row_y, label, DESKTOP_TEXT,
                             index % 2 == 0 ? 0xf0f3f9 : 0xffffff, 1);
        }
        framebuffer_rect(slot->x + 18, slot->y + slot->height - 52,
                         slot->width - 36, 1, 0xdce2ed);
        char preview_lines[DESKTOP_FILE_PREVIEW_MAX + 4];
        const char *preview = desktop_file_preview[0] != '\0'
                                  ? desktop_file_preview
                                  : "Select a file to preview";
        const size_t line_capacity =
            slot->width > 48 ? (size_t)((slot->width - 48) / 6) : 1;
        size_t source_index = 0;
        size_t output_index = 0;
        for (size_t line = 0; line < 3 && preview[source_index] != '\0'; ++line) {
            size_t column = 0;
            while (preview[source_index] != '\0' &&
                   column < line_capacity &&
                   output_index + 1 < sizeof(preview_lines)) {
                preview_lines[output_index++] = preview[source_index++];
                ++column;
            }
            if (preview[source_index] != '\0' && line < 2 &&
                output_index + 1 < sizeof(preview_lines)) {
                preview_lines[output_index++] = '\n';
            }
        }
        preview_lines[output_index] = '\0';
        framebuffer_text(slot->x + 20, slot->y + slot->height - 42,
                         preview_lines, DESKTOP_MUTED, DESKTOP_SURFACE, 1);
        return;
    }
    if (desktop_window_name_matches(slot->name, "NETWORK")) {
        framebuffer_text(slot->x + 20, slot->y + 54,
                         desktop_status.network_ready
                             ? "e1000 online" : "No supported NIC",
                         desktop_status.network_ready ? 0x18794e : 0xb45309,
                         DESKTOP_SURFACE, 1);
        framebuffer_text(slot->x + 20, slot->y + 72,
                         "IPv4 10.0.2.15 / gateway .2",
                         DESKTOP_MUTED, DESKTOP_SURFACE, 1);
        char counters[32];
        desktop_format_uint(counters, sizeof(counters),
                            desktop_status.udp_echoes);
        framebuffer_text(slot->x + 20, slot->y + 90, "UDP echo replies:",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_text(slot->x + 130, slot->y + 90, counters,
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        desktop_format_uint(counters, sizeof(counters),
                            desktop_status.tcp_echoes);
        framebuffer_text(slot->x + 20, slot->y + 108, "TCP echo replies:",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_text(slot->x + 130, slot->y + 108, counters,
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_rect(slot->x + 18, slot->y + 128,
                         slot->width - 36, 24, DESKTOP_ACCENT);
        framebuffer_text(slot->x + 28, slot->y + 136,
                         desktop_status.gateway_reachable
                             ? "GATEWAY REACHABLE" : "PING GATEWAY",
                         0xffffff, DESKTOP_ACCENT, 1);
        return;
    }
    if (desktop_window_name_matches(slot->name, "AUDIO")) {
        framebuffer_text(slot->x + 20, slot->y + 54, "PC SPEAKER OUTPUT",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_rect(slot->x + 20, slot->y + 78, slot->width - 40, 34,
                         DESKTOP_ACCENT);
        framebuffer_text(slot->x + 34, slot->y + 91, "PLAY TEST TONE",
                         0xffffff, DESKTOP_ACCENT, 1);
        framebuffer_text(slot->x + 20, slot->y + 126,
                         "880 Hz - PC speaker, no PCM mixer",
                         DESKTOP_MUTED, DESKTOP_SURFACE, 1);
        return;
    }
    if (desktop_window_name_matches(slot->name, "SETTINGS")) {
        const uint64_t sidebar_width = slot->width > 300 ? 124 : 88;
        const uint64_t main_x = slot->x + sidebar_width + 24;
        const uint64_t main_width =
            slot->width > sidebar_width + 44
                ? slot->width - sidebar_width - 44 : 120;
        framebuffer_rect(slot->x + 1, slot->y + 35, sidebar_width,
                         slot->height - 36, 0xedf1f7);
        framebuffer_text(slot->x + 16, slot->y + 50, "SETTINGS",
                         DESKTOP_TEXT, 0xedf1f7, 1);
        framebuffer_rect(slot->x + 12, slot->y + 68, sidebar_width - 20,
                         28, 0xdde7fb);
        framebuffer_text(slot->x + 18, slot->y + 78, "PROFILE",
                         DESKTOP_ACCENT, 0xdde7fb, 1);
        framebuffer_text(slot->x + 18, slot->y + 110, "PERSONALIZE",
                         DESKTOP_MUTED, 0xedf1f7, 1);
        framebuffer_text(slot->x + 18, slot->y + 142, "SERVICES",
                         DESKTOP_MUTED, 0xedf1f7, 1);
        framebuffer_text(slot->x + 18, slot->y + 174, "MOTION",
                         DESKTOP_MUTED, 0xedf1f7, 1);

        framebuffer_text(main_x, slot->y + 50, "YOUR DEVOS",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_text(main_x, slot->y + 68, desktop_username,
                         DESKTOP_ACCENT, DESKTOP_SURFACE, 1);
        framebuffer_text(main_x + main_width / 2, slot->y + 68,
                         desktop_hostname, DESKTOP_MUTED,
                         DESKTOP_SURFACE, 1);
        framebuffer_rect(main_x, slot->y + 86, main_width, 1, 0xdce2ed);
        framebuffer_text(main_x, slot->y + 96, "WALLPAPER",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);

        const uint64_t tile_gap = 8;
        const uint64_t tile_width =
            main_width > 2 * tile_gap
                ? (main_width - 2 * tile_gap) / DESKTOP_WALLPAPER_COUNT : 1;
        for (size_t index = 0; index < DESKTOP_WALLPAPER_COUNT; ++index) {
            const uint64_t tile_x =
                main_x + index * (tile_width + tile_gap);
            const struct desktop_wallpaper *wallpaper =
                &desktop_wallpapers[index];
            framebuffer_rect(tile_x, slot->y + 116, tile_width, 59,
                             index == desktop_selected_wallpaper
                                 ? DESKTOP_ACCENT : 0xdce2ed);
            framebuffer_rect(tile_x + 2, slot->y + 118,
                             tile_width > 4 ? tile_width - 4 : 1, 38,
                             wallpaper->top);
            framebuffer_rect(tile_x + 2, slot->y + 137,
                             tile_width > 4 ? tile_width - 4 : 1, 19,
                             wallpaper->bottom);
            framebuffer_rect(tile_x + 2, slot->y + 153,
                             tile_width > 4 ? tile_width - 4 : 1, 4,
                             wallpaper->accent);
            framebuffer_text(tile_x + 4, slot->y + 160, wallpaper->name,
                             index == desktop_selected_wallpaper
                                 ? 0xffffff : DESKTOP_TEXT,
                             index == desktop_selected_wallpaper
                                 ? DESKTOP_ACCENT : 0xdce2ed, 1);
        }

        framebuffer_text(main_x, slot->y + 187, "SYSTEM SERVICES",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        const uint64_t card_gap = 8;
        const uint64_t card_width =
            main_width > card_gap ? (main_width - card_gap) / 2 : 1;
        const uint64_t card_height = slot->height > 330 ? 35 : 24;
        const uint64_t card_y = slot->y + 204;
        const char *service_names[4] = {
            "STORAGE", "NETWORK", "AUDIO", "DESKTOP"
        };
        const char *service_states[4] = {
            desktop_status.storage_ready ? "READY" : "RAM ONLY",
            desktop_status.network_ready ? "ONLINE" : "OFFLINE",
            desktop_status.audio_ready ? "PC SPEAKER" : "OFFLINE",
            "RUNNING"
        };
        const uint8_t service_ready[4] = {
            desktop_status.storage_ready,
            desktop_status.network_ready,
            desktop_status.audio_ready,
            1
        };
        for (size_t index = 0; index < 4; ++index) {
            const uint64_t column = index % 2;
            const uint64_t row = index / 2;
            const uint64_t card_x =
                main_x + column * (card_width + card_gap);
            const uint64_t y = card_y + row * (card_height + 5);
            framebuffer_rect(card_x, y, card_width, card_height,
                             0xf0f3f9);
            framebuffer_rect(card_x, y, 3, card_height,
                             service_ready[index] ? 0x34a77a : 0xd69a42);
            framebuffer_text(card_x + 9, y + 4, service_names[index],
                             DESKTOP_TEXT, 0xf0f3f9, 1);
            if (card_height >= 30) {
                framebuffer_text(card_x + 9, y + 19,
                                 service_states[index],
                                 service_ready[index] ? 0x18794e : 0xb45309,
                                 0xf0f3f9, 1);
            }
        }

        const uint64_t motion_y =
            slot->height > 300 ? slot->y + slot->height - 31
                               : slot->y + 260;
        framebuffer_rect(main_x, motion_y, main_width, 1, 0xdce2ed);
        framebuffer_text(main_x, motion_y + 8,
                         desktop_motion_enabled
                             ? "MOTION  ON"
                             : "MOTION  OFF",
                         DESKTOP_TEXT, DESKTOP_SURFACE, 1);
        framebuffer_rect(main_x + main_width - 54, motion_y + 5,
                         48, 17,
                         desktop_motion_enabled ? DESKTOP_ACCENT : 0x9aa5b5);
        framebuffer_text(main_x + main_width - 49, motion_y + 10,
                         desktop_motion_enabled ? "ON" : "OFF",
                         0xffffff,
                         desktop_motion_enabled ? DESKTOP_ACCENT : 0x9aa5b5,
                         1);
        return;
    }
    if (slot->focused) {
        framebuffer_text(slot->x + 20, slot->y + 54, "focused", DESKTOP_ACCENT,
                         DESKTOP_SURFACE, 1);
    } else {
        framebuffer_text(slot->x + 20, slot->y + 54, "ready", 0x18794e,
                         DESKTOP_SURFACE, 1);
    }
    framebuffer_rect(slot->x + 20, slot->y + 86, slot->width - 40, 3, 0xe7ebf2);
    framebuffer_text(slot->x + 20, slot->y + 98, "desktop", DESKTOP_MUTED,
                 DESKTOP_SURFACE, 1);
}

static uint64_t desktop_window_animation_offset(void)
{
    const uint64_t remaining =
        DESKTOP_ANIMATION_STEPS - desktop_animation_frame;
    return 20 * remaining * remaining /
           (DESKTOP_ANIMATION_STEPS * DESKTOP_ANIMATION_STEPS);
}

static uint64_t desktop_window_close_offset(void)
{
    return 20 - desktop_window_animation_offset();
}

static void desktop_window_render_slot(size_t slot_index)
{
    if (slot_index >= DESKTOP_WINDOW_MAX || !desktop_windows[slot_index].open) {
        return;
    }

    struct desktop_window_slot visual = desktop_windows[slot_index];
    if (desktop_animation == DESKTOP_ANIMATION_WINDOW_OPEN &&
        slot_index == desktop_animation_window) {
        visual.y += desktop_window_animation_offset();
    }
    desktop_window_render_slot_content(&visual);
}

static void desktop_window_render_all(void)
{
    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open && !desktop_windows[index].focused) {
            desktop_window_render_slot(index);
        }
    }
    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open && desktop_windows[index].focused) {
            desktop_window_render_slot(index);
        }
    }
    if (desktop_animation == DESKTOP_ANIMATION_WINDOW_CLOSE &&
        desktop_closing_window_visible) {
        struct desktop_window_slot visual = desktop_closing_window;
        visual.y += desktop_window_close_offset();
        desktop_window_render_slot_content(&visual);
    }
}

static void desktop_start_menu_draw(void)
{
    if (!desktop_menu_visible || active_framebuffer == NULL) {
        return;
    }

    const uint64_t bottom = active_framebuffer->height > 54
                                ? active_framebuffer->height - 54 : 0;
    const uint64_t menu_height = bottom > 302 ? 302 : bottom;
    const uint64_t menu_top = bottom - menu_height;
    uint64_t offset = desktop_window_animation_offset();
    if (desktop_animation == DESKTOP_ANIMATION_MENU_CLOSE) {
        offset = 20 - offset;
    } else if (desktop_animation != DESKTOP_ANIMATION_MENU_OPEN) {
        offset = 0;
    }
    const uint64_t y = menu_top + offset;
    framebuffer_rect(8, y - 4, 228, menu_height + 4,
                     0x0c1220);
    framebuffer_rect(12, y, 220, menu_height,
                     DESKTOP_SURFACE);
    framebuffer_rect(12, y, 220, 36, 0xe9edf5);
    framebuffer_rect(12, y, 3, menu_height, DESKTOP_ACCENT);
    framebuffer_text(26, y + 11, "DEVOS",
                     DESKTOP_TEXT, 0xe9edf5, 1);
    framebuffer_text(28, y + 52, "TERMINAL",
                     DESKTOP_TEXT, DESKTOP_SURFACE, 1);
    framebuffer_text(28, y + 94, "FILES",
                     DESKTOP_TEXT, DESKTOP_SURFACE, 1);
    framebuffer_text(28, y + 136, "NETWORK",
                     DESKTOP_TEXT, DESKTOP_SURFACE, 1);
    framebuffer_text(28, y + 178, "SETTINGS",
                     DESKTOP_TEXT, DESKTOP_SURFACE, 1);
    framebuffer_text(28, y + 220, "AUDIO",
                     DESKTOP_TEXT, DESKTOP_SURFACE, 1);
    framebuffer_rect(28, y + 262, 184, 1, 0xdce2ed);
    framebuffer_text(28, y + 276, desktop_username,
                     DESKTOP_MUTED, DESKTOP_SURFACE, 1);
}

static const char *const cursor_shape[CURSOR_HEIGHT] = {
    "B...........",
    "BB..........",
    "BWB.........",
    "BWWB........",
    "BWWWB.......",
    "BWWWWB......",
    "BWWWWWB.....",
    "BWWWWWWB....",
    "BWWWWWWWB...",
    "BWWBBBBBB...",
    "BWBWB.......",
    "BB.BWB......",
    "B...BWB.....",
    ".....BWB....",
    "......BB....",
    "............",
};

static uint32_t channel(uint8_t value, uint8_t width, uint8_t shift)
{
    if (width == 0 || width > 8 || shift >= 32 || width > 32 - shift) {
        return 0;
    }
    return ((uint32_t)value >> (8 - width)) << shift;
}

static uint32_t pixel_value(uint32_t color)
{
    const uint32_t red = (color >> 16) & 0xff;
    const uint32_t green = (color >> 8) & 0xff;
    const uint32_t blue = color & 0xff;

    return channel((uint8_t)red, active_framebuffer->red_mask_size,
                   active_framebuffer->red_mask_shift) |
           channel((uint8_t)green, active_framebuffer->green_mask_size,
                   active_framebuffer->green_mask_shift) |
           channel((uint8_t)blue, active_framebuffer->blue_mask_size,
                   active_framebuffer->blue_mask_shift);
}

static volatile uint32_t *framebuffer_pixel(uint64_t x, uint64_t y)
{
    return (volatile uint32_t *)((uintptr_t)active_framebuffer->address +
                                 y * active_framebuffer->pitch) + x;
}

static void cursor_restore(void)
{
    if (!cursor_visible || active_framebuffer == NULL) {
        return;
    }
    for (uint8_t row = 0; row < cursor_background_height; ++row) {
        for (uint8_t column = 0; column < cursor_background_width; ++column) {
            *framebuffer_pixel(cursor_x + column, cursor_y + row) =
                cursor_background[row][column];
        }
    }
    cursor_visible = 0;
}

static void cursor_draw(void)
{
    if (active_framebuffer == NULL || active_framebuffer->width == 0 ||
        active_framebuffer->height == 0) {
        return;
    }
    if (cursor_x >= active_framebuffer->width) {
        cursor_x = active_framebuffer->width - 1;
    }
    if (cursor_y >= active_framebuffer->height) {
        cursor_y = active_framebuffer->height - 1;
    }

    cursor_background_width = (uint8_t)(
        active_framebuffer->width - cursor_x < CURSOR_WIDTH
            ? active_framebuffer->width - cursor_x : CURSOR_WIDTH);
    cursor_background_height = (uint8_t)(
        active_framebuffer->height - cursor_y < CURSOR_HEIGHT
            ? active_framebuffer->height - cursor_y : CURSOR_HEIGHT);
    for (uint8_t row = 0; row < cursor_background_height; ++row) {
        for (uint8_t column = 0; column < cursor_background_width; ++column) {
            cursor_background[row][column] =
                *framebuffer_pixel(cursor_x + column, cursor_y + row);
            if (cursor_shape[row][column] == 'B') {
                *framebuffer_pixel(cursor_x + column, cursor_y + row) =
                    pixel_value(0x101521);
            } else if (cursor_shape[row][column] == 'W') {
                *framebuffer_pixel(cursor_x + column, cursor_y + row) =
                    pixel_value(0xf3f6fc);
            }
        }
    }
    cursor_visible = 1;
}

int framebuffer_init(struct limine_framebuffer *framebuffer)
{
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->width == 0 || framebuffer->height == 0 ||
        framebuffer->bpp != 32 ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->red_mask_size == 0 ||
        framebuffer->green_mask_size == 0 ||
        framebuffer->blue_mask_size == 0 ||
        framebuffer->red_mask_size > 8 ||
        framebuffer->green_mask_size > 8 ||
        framebuffer->blue_mask_size > 8 ||
        framebuffer->red_mask_shift > 24 ||
        framebuffer->green_mask_shift > 24 ||
        framebuffer->blue_mask_shift > 24 ||
        framebuffer->width > UINT64_MAX / sizeof(uint32_t) ||
        framebuffer->pitch < framebuffer->width * sizeof(uint32_t) ||
        framebuffer->pitch % sizeof(uint32_t) != 0 ||
        framebuffer->height >
            (UINTPTR_MAX - (uintptr_t)framebuffer->address) /
                framebuffer->pitch) {
        return 0;
    }

    active_framebuffer = framebuffer;
    return 1;
}

void framebuffer_clear(uint32_t color)
{
    if (active_framebuffer == NULL) {
        return;
    }

    framebuffer_rect(0, 0, active_framebuffer->width,
                     active_framebuffer->height, color);
}

void framebuffer_rect(uint64_t x, uint64_t y, uint64_t width,
                      uint64_t height, uint32_t color)
{
    if (active_framebuffer == NULL || x >= active_framebuffer->width ||
        y >= active_framebuffer->height) {
        return;
    }

    if (width > active_framebuffer->width - x) {
        width = active_framebuffer->width - x;
    }
    if (height > active_framebuffer->height - y) {
        height = active_framebuffer->height - y;
    }

    const uint32_t value = pixel_value(color);
    for (uint64_t row = 0; row < height; ++row) {
        volatile uint32_t *pixels =
            (volatile uint32_t *)((uintptr_t)active_framebuffer->address +
                                  (y + row) * active_framebuffer->pitch) + x;
        for (uint64_t column = 0; column < width; ++column) {
            pixels[column] = value;
        }
    }
}

uint64_t framebuffer_width(void)
{
    return active_framebuffer != NULL ? active_framebuffer->width : 0;
}

uint64_t framebuffer_height(void)
{
    return active_framebuffer != NULL ? active_framebuffer->height : 0;
}

void framebuffer_cursor_set(uint64_t x, uint64_t y)
{
    const uint8_t should_show = cursor_visible;
    cursor_restore();
    cursor_x = x;
    cursor_y = y;
    if (should_show) {
        cursor_draw();
    }
}

void framebuffer_cursor_hide(void)
{
    cursor_restore();
}

void framebuffer_cursor_show(void)
{
    if (!cursor_visible) {
        cursor_draw();
    }
}

static uint8_t glyph_row(char character, uint8_t row)
{
    static const uint8_t font[][7] = {
        { 14, 17, 17, 31, 17, 17, 17 }, { 30, 17, 17, 30, 17, 17, 30 },
        { 14, 17, 16, 16, 16, 17, 14 }, { 30, 17, 17, 17, 17, 17, 30 },
        { 31, 16, 16, 30, 16, 16, 31 }, { 31, 16, 16, 30, 16, 16, 16 },
        { 14, 17, 16, 23, 17, 17, 15 }, { 17, 17, 17, 31, 17, 17, 17 },
        { 14, 4, 4, 4, 4, 4, 14 },     { 7, 2, 2, 2, 18, 18, 12 },
        { 17, 18, 20, 24, 20, 18, 17 }, { 16, 16, 16, 16, 16, 16, 31 },
        { 17, 27, 21, 21, 17, 17, 17 }, { 17, 25, 21, 19, 17, 17, 17 },
        { 14, 17, 17, 17, 17, 17, 14 }, { 30, 17, 17, 30, 16, 16, 16 },
        { 14, 17, 17, 17, 21, 18, 13 }, { 30, 17, 17, 30, 20, 18, 17 },
        { 15, 16, 16, 14, 1, 1, 30 },   { 31, 4, 4, 4, 4, 4, 4 },
        { 17, 17, 17, 17, 17, 17, 14 }, { 17, 17, 17, 17, 17, 10, 4 },
        { 17, 17, 17, 21, 21, 21, 10 }, { 17, 17, 10, 4, 10, 17, 17 },
        { 17, 17, 10, 4, 4, 4, 4 },     { 31, 1, 2, 4, 8, 16, 31 },
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },
        { 14, 17, 1, 2, 4, 8, 31 },     { 30, 1, 1, 14, 1, 1, 30 },
        { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 16, 30, 1, 1, 30 },
        { 14, 16, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
        { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 1, 14 },
        { 0, 0, 0, 0, 0, 0, 0 },       { 0, 0, 0, 31, 0, 0, 0 },
        { 0, 4, 4, 0, 4, 4, 0 },       { 0, 0, 0, 0, 0, 4, 8 },
        { 0, 0, 0, 0, 0, 0, 4 },
    };

    if (row >= 7) {
        return 0;
    }
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    if (character >= 'A' && character <= 'Z') {
        return font[character - 'A'][row];
    }
    if (character >= '0' && character <= '9') {
        return font[26 + character - '0'][row];
    }
    switch (character) {
    case ' ':
        return 0;
    case '-':
        return font[36][row];
    case ':':
        return font[37][row];
    case '.':
        return font[38][row];
    case '>':
        return font[39][row];
    default:
        return 0;
    }
}

void framebuffer_text(uint64_t x, uint64_t y, const char *text,
                      uint32_t color, uint32_t background, uint32_t scale)
{
    if (active_framebuffer == NULL || text == NULL || scale == 0) {
        return;
    }

    const uint64_t start_x = x;
    while (*text != '\0') {
        if (*text == '\n') {
            x = start_x;
            y += 9 * scale;
            ++text;
            continue;
        }
        for (uint8_t row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_row(*text, row);
            for (uint8_t column = 0; column < 5; ++column) {
                const uint32_t pixel = (bits & (1U << (4 - column)))
                                           ? color
                                           : background;
                framebuffer_rect(x + column * scale, y + row * scale,
                                 scale, scale, pixel);
            }
        }
        x += 6 * scale;
        ++text;
        if (x >= active_framebuffer->width) {
            break;
        }
    }
}

static void desktop_draw_start_button(uint64_t width)
{
    const uint64_t button_x = 16;
    const uint64_t button_y = active_framebuffer->height > 42
                                  ? active_framebuffer->height - 42 : 0;
    const uint64_t button_width = width > 90 ? 90 : width;
    const uint64_t button_height = 28;
    framebuffer_rect(button_x, button_y, button_width, button_height, 0x202b3e);
    framebuffer_rect(button_x + 13, button_y + 8, 5, 5, 0x72a1ff);
    framebuffer_rect(button_x + 20, button_y + 8, 5, 5, 0x72a1ff);
    framebuffer_rect(button_x + 13, button_y + 15, 5, 5, 0x72a1ff);
    framebuffer_rect(button_x + 20, button_y + 15, 5, 5, 0x72a1ff);
    framebuffer_text(button_x + 34, button_y + 9, "START",
                     0xe5ebf5, 0x202b3e, 1);
}

static void desktop_draw_memory_text(uint64_t usable_memory_mib)
{
    char memory_text[32] = "memory unknown";
    if (usable_memory_mib != 0) {
        uint64_t value = usable_memory_mib;
        size_t index = 0;
        char digits[24];
        do {
            digits[index++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && index < sizeof(digits));
        size_t output = 0;
        while (index > 0 && output + 1 < sizeof(memory_text)) {
            memory_text[output++] = digits[--index];
        }
        memory_text[output++] = ' ';
        memory_text[output++] = 'M';
        memory_text[output++] = 'i';
        memory_text[output++] = 'B';
        memory_text[output] = '\0';
    }
    framebuffer_text(active_framebuffer->width > 480 ? active_framebuffer->width - 220 : 12,
                     38, memory_text, 0xc5d0e2, 0x18243a, 1);
}

void desktop_window_init(void)
{
    memset(desktop_windows, 0, sizeof(desktop_windows));
    desktop_initialized = 1;
    desktop_start_menu_open = 0;
    desktop_menu_visible = 0;
    desktop_animation = DESKTOP_ANIMATION_NONE;
    desktop_animation_frame = 0;
    desktop_has_drawn = 0;
    desktop_closing_window_visible = 0;
    desktop_selected_wallpaper = 0;
    desktop_motion_enabled = 1;
    desktop_previous_buttons = 0;
    desktop_drag_slot = -1;

    desktop_window_open("SYSTEM", 90, 60, 300, 160, 0x2a74d1);
    const uint64_t files_x = active_framebuffer->width > 700
                                 ? 400
                                 : active_framebuffer->width > 500
                                       ? active_framebuffer->width - 300 : 180;
    const uint64_t files_width = active_framebuffer->width > files_x + 360
                                     ? 360
                                     : active_framebuffer->width - files_x;
    const uint64_t files_height = active_framebuffer->height > 360
                                      ? 300 : active_framebuffer->height - 100;
    desktop_window_open("FILES", files_x, 80, files_width, files_height,
                        0x38a169);
    desktop_window_focus("SYSTEM");
}

int desktop_window_open(const char *name, uint64_t x, uint64_t y, uint64_t width,
                       uint64_t height, uint32_t accent)
{
    const char *safe_name = desktop_window_lookup_name(name);
    if (safe_name == NULL || desktop_name_length(safe_name) >=
                                 sizeof(desktop_windows[0].name) ||
        width < 120 || height < 90) {
        return 0;
    }

    if (!desktop_initialized) {
        desktop_window_init();
    }

    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open &&
            desktop_window_name_matches(desktop_windows[index].name, safe_name)) {
            desktop_windows[index].x = x;
            desktop_windows[index].y = y;
            desktop_windows[index].width = width;
            desktop_windows[index].height = height;
            desktop_windows[index].accent = accent;
            desktop_window_focus(safe_name);
            return 1;
        }
    }

    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (!desktop_windows[index].open) {
            memset(&desktop_windows[index], 0, sizeof(desktop_windows[index]));
            desktop_stash_name(desktop_windows[index].name,
                              sizeof(desktop_windows[index].name), safe_name);
            desktop_windows[index].open = 1;
            desktop_windows[index].focused = 1;
            desktop_windows[index].x = x;
            desktop_windows[index].y = y;
            desktop_windows[index].width = width;
            desktop_windows[index].height = height;
            desktop_windows[index].accent = accent;
            for (size_t other = 0; other < DESKTOP_WINDOW_MAX; ++other) {
                if (other != index && desktop_windows[other].open) {
                    desktop_windows[other].focused = 0;
                }
            }
            if (desktop_has_drawn) {
                desktop_start_menu_open = 0;
                desktop_menu_visible = 0;
                desktop_closing_window_visible = 0;
                desktop_animation_window = index;
                desktop_animation_start(DESKTOP_ANIMATION_WINDOW_OPEN);
            }
            return 1;
        }
    }

    return 0;
}

int desktop_window_close(const char *name)
{
    const char *safe_name = desktop_window_lookup_name(name);
    if (safe_name == NULL) {
        return 0;
    }

    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open &&
            desktop_window_name_matches(desktop_windows[index].name, safe_name)) {
            if (desktop_has_drawn) {
                desktop_closing_window = desktop_windows[index];
                desktop_closing_window_visible = 1;
                desktop_start_menu_open = 0;
                desktop_menu_visible = 0;
                desktop_animation_start(DESKTOP_ANIMATION_WINDOW_CLOSE);
            }
            memset(&desktop_windows[index], 0, sizeof(desktop_windows[index]));
            return 1;
        }
    }
    return 0;
}

int desktop_window_is_open(const char *name)
{
    return desktop_window_find_slot(name, NULL);
}

size_t desktop_window_count(void)
{
    size_t count = 0;
    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open) {
            ++count;
        }
    }
    return count;
}

const char *desktop_window_name(size_t index)
{
    if (index >= DESKTOP_WINDOW_MAX || !desktop_windows[index].open) {
        return NULL;
    }
    return desktop_windows[index].name;
}

void desktop_window_focus(const char *name)
{
    const char *safe_name = desktop_window_lookup_name(name);
    if (safe_name == NULL) {
        return;
    }

    for (size_t index = 0; index < DESKTOP_WINDOW_MAX; ++index) {
        if (desktop_windows[index].open) {
            desktop_windows[index].focused =
                desktop_window_name_matches(desktop_windows[index].name, safe_name);
        }
    }
}

int desktop_file_manager_update(const char *path,
                               const struct desktop_file_item *items,
                               size_t count, const char *preview)
{
    if (path == NULL || count > DESKTOP_FILE_ITEMS_MAX ||
        (count != 0 && items == NULL)) {
        return 0;
    }

    desktop_copy_string(desktop_file_path, sizeof(desktop_file_path), path);
    desktop_file_item_count = count;
    memset(desktop_file_items, 0, sizeof(desktop_file_items));
    for (size_t index = 0; index < count; ++index) {
        desktop_copy_string(desktop_file_items[index].name,
                            sizeof(desktop_file_items[index].name),
                            items[index].name);
        desktop_file_items[index].size = items[index].size;
        desktop_file_items[index].is_directory = items[index].is_directory;
    }
    desktop_copy_string(desktop_file_preview, sizeof(desktop_file_preview), preview);
    return 1;
}

static int desktop_open_app(const char *name)
{
    if (desktop_window_is_open(name)) {
        desktop_window_focus(name);
        return 1;
    }

    if (desktop_window_name_matches(name, "SETTINGS") &&
        active_framebuffer->width >= 240 &&
        active_framebuffer->height >= 180) {
        const uint64_t width = active_framebuffer->width > 64
                                   ? active_framebuffer->width - 64
                                   : active_framebuffer->width;
        const uint64_t height = active_framebuffer->height > 110
                                    ? active_framebuffer->height - 110
                                    : active_framebuffer->height;
        const uint64_t x = (active_framebuffer->width - width) / 2;
        const uint64_t y = 42;
        if (width >= 120 && height >= 90) {
            return desktop_window_open("SETTINGS", x, y, width, height,
                                       0x815ac0);
        }
    }

    const size_t count = desktop_window_count();
    const uint64_t x = 100 + (count % 4) * 32;
    const uint64_t y = 70 + (count % 4) * 28;
    uint32_t accent = 0x2a74d1;
    if (desktop_window_name_matches(name, "FILES")) {
        accent = 0x38a169;
    } else if (desktop_window_name_matches(name, "NETWORK")) {
        accent = 0xf29f05;
    } else if (desktop_window_name_matches(name, "SETTINGS")) {
        accent = 0x815ac0;
    } else if (desktop_window_name_matches(name, "AUDIO")) {
        accent = 0x2f8cff;
    }
    return desktop_window_open(name, x, y, 300, 180, accent);
}

int desktop_pointer_event(uint64_t x, uint64_t y, uint8_t buttons)
{
    if (active_framebuffer == NULL || !desktop_initialized) {
        return DESKTOP_ACTION_NONE;
    }

    const uint8_t primary_down = buttons & 0x01;
    const uint8_t primary_pressed =
        primary_down && (desktop_previous_buttons & 0x01) == 0;
    const uint8_t primary_released =
        !primary_down && (desktop_previous_buttons & 0x01) != 0;
    desktop_previous_buttons = buttons;

    if (desktop_drag_slot >= 0 && primary_down) {
        struct desktop_window_slot *slot =
            &desktop_windows[(size_t)desktop_drag_slot];
        uint64_t new_x = x > desktop_drag_offset_x ? x - desktop_drag_offset_x : 0;
        uint64_t new_y = y > desktop_drag_offset_y ? y - desktop_drag_offset_y : 0;
        const uint64_t max_x = active_framebuffer->width > slot->width
                                   ? active_framebuffer->width - slot->width : 0;
        const uint64_t max_y = active_framebuffer->height > slot->height + 54
                                   ? active_framebuffer->height - slot->height - 54 : 0;
        slot->x = new_x > max_x ? max_x : new_x;
        slot->y = new_y > max_y ? max_y : new_y;
        return DESKTOP_ACTION_REDRAW;
    }
    if (primary_released) {
        desktop_drag_slot = -1;
    }
    if (!primary_pressed) {
        return DESKTOP_ACTION_NONE;
    }

    const uint64_t height = active_framebuffer->height;
    const uint64_t taskbar_top = height > 54 ? height - 54 : 0;
    const uint64_t menu_bottom = taskbar_top;
    const uint64_t menu_height = taskbar_top > 302 ? 302 : taskbar_top;
    const uint64_t menu_top = menu_bottom - menu_height;

    if (desktop_start_menu_open && x >= 12 && x < 232 &&
        y >= menu_top && y < menu_bottom) {
        const uint64_t row_y = y - menu_top;
        if (row_y >= 36 && row_y < 78) {
            desktop_start_menu_open = 0;
            desktop_menu_visible = 0;
            desktop_animation = DESKTOP_ANIMATION_NONE;
            return DESKTOP_ACTION_OPEN_TERMINAL;
        }
        const char *app = NULL;
        if (row_y >= 78 && row_y < 120) {
            app = "FILES";
        } else if (row_y >= 120 && row_y < 162) {
            app = "NETWORK";
        } else if (row_y >= 162 && row_y < 204) {
            app = "SETTINGS";
        } else if (row_y >= 204 && row_y < 246) {
            app = "AUDIO";
        }
        desktop_start_menu_open = 0;
        desktop_menu_visible = 0;
        desktop_animation = DESKTOP_ANIMATION_NONE;
        if (app != NULL) {
            desktop_open_app(app);
            return DESKTOP_ACTION_REDRAW;
        }
        return DESKTOP_ACTION_REDRAW;
    }

    if (y >= taskbar_top) {
        if (x >= 16 && x < 106 && y < taskbar_top + 42) {
            desktop_start_menu_open = !desktop_start_menu_open;
            desktop_menu_visible = 1;
            desktop_animation_start(desktop_start_menu_open
                ? DESKTOP_ANIMATION_MENU_OPEN : DESKTOP_ANIMATION_MENU_CLOSE);
            return DESKTOP_ACTION_REDRAW;
        }
        if (x >= 120 && x < 192) {
            desktop_start_menu_open = 0;
            desktop_menu_visible = 0;
            desktop_animation = DESKTOP_ANIMATION_NONE;
            return DESKTOP_ACTION_OPEN_TERMINAL;
        }
        if (x >= 208 && x < 272) {
            desktop_menu_visible = 0;
            desktop_open_app("FILES");
            return DESKTOP_ACTION_REDRAW;
        }
        if (x >= 286 && x < 358) {
            desktop_menu_visible = 0;
            desktop_open_app("SETTINGS");
            return DESKTOP_ACTION_REDRAW;
        }
        desktop_start_menu_open = 0;
        if (desktop_menu_visible) {
            desktop_animation_start(DESKTOP_ANIMATION_MENU_CLOSE);
        }
        return DESKTOP_ACTION_REDRAW;
    }

    desktop_start_menu_open = 0;
    if (desktop_menu_visible) {
        desktop_animation_start(DESKTOP_ANIMATION_MENU_CLOSE);
    }
    for (size_t reverse = DESKTOP_WINDOW_MAX; reverse > 0; --reverse) {
        const size_t index = reverse - 1;
        struct desktop_window_slot *slot = &desktop_windows[index];
        if (!slot->open || x < slot->x || y < slot->y ||
            x >= slot->x + slot->width || y >= slot->y + slot->height) {
            continue;
        }

        desktop_window_focus(slot->name);
        if (desktop_window_name_matches(slot->name, "FILES") &&
            x >= slot->x + 12 && x < slot->x + slot->width - 12) {
            if (y >= slot->y + 62 && y < slot->y + 80) {
                return DESKTOP_ACTION_FILE_PARENT;
            }
            if (y >= slot->y + 82 && y < slot->y + 82 +
                                           DESKTOP_FILE_ITEMS_MAX * 16) {
                const size_t item_index =
                    (size_t)((y - (slot->y + 82)) / 16);
                if (item_index < desktop_file_item_count) {
                    return DESKTOP_ACTION_FILE_ITEM_BASE + (int)item_index;
                }
            }
            return DESKTOP_ACTION_REDRAW;
        }
        if (desktop_window_name_matches(slot->name, "AUDIO") &&
            x >= slot->x + 12 && x < slot->x + slot->width - 12 &&
            y >= slot->y + 78 && y < slot->y + 112) {
            return DESKTOP_ACTION_AUDIO_TEST_TONE;
        }
        if (desktop_window_name_matches(slot->name, "NETWORK") &&
            x >= slot->x + 12 && x < slot->x + slot->width - 12 &&
            y >= slot->y + 128 && y < slot->y + 152) {
            return desktop_status.gateway_reachable
                       ? DESKTOP_ACTION_REDRAW : DESKTOP_ACTION_NETWORK_PING;
        }
        if (desktop_window_name_matches(slot->name, "SETTINGS")) {
            const uint64_t sidebar_width =
                slot->width > 300 ? 124 : 88;
            const uint64_t main_x = slot->x + sidebar_width + 24;
            const uint64_t main_width =
                slot->width > sidebar_width + 44
                    ? slot->width - sidebar_width - 44 : 120;
            if (y >= slot->y + 114 && y < slot->y + 179 &&
                x >= main_x &&
                x < main_x + main_width) {
                const uint64_t tile_gap = 8;
                const uint64_t tile_width =
                    main_width > 2 * tile_gap
                        ? (main_width - 2 * tile_gap) /
                              DESKTOP_WALLPAPER_COUNT
                        : 1;
                const uint64_t offset = x - main_x;
                const uint64_t tile_stride = tile_width + tile_gap;
                const size_t wallpaper = (size_t)(offset / tile_stride);
                if (wallpaper < DESKTOP_WALLPAPER_COUNT &&
                    offset % tile_stride < tile_width) {
                    desktop_selected_wallpaper = wallpaper;
                    return DESKTOP_ACTION_WALLPAPER_BASE + (int)wallpaper;
                }
            }
            const uint64_t motion_y =
                slot->height > 300 ? slot->y + slot->height - 31
                                   : slot->y + 260;
            if (y >= motion_y + 4 && y < motion_y + 24 &&
                x >= main_x && x < main_x + main_width) {
                desktop_motion_enabled = !desktop_motion_enabled;
                if (!desktop_motion_enabled) {
                    desktop_animation = DESKTOP_ANIMATION_NONE;
                    desktop_animation_frame = 0;
                    desktop_menu_visible = desktop_start_menu_open;
                    desktop_closing_window_visible = 0;
                }
                return DESKTOP_ACTION_ANIMATION_TOGGLE;
            }
        }
        if (y < slot->y + 28) {
            if (x < slot->x + 24) {
                desktop_window_close(slot->name);
                return DESKTOP_ACTION_REDRAW;
            }
            desktop_drag_slot = (int)index;
            desktop_drag_offset_x = x - slot->x;
            desktop_drag_offset_y = y - slot->y;
        }
        return DESKTOP_ACTION_REDRAW;
    }
    return DESKTOP_ACTION_REDRAW;
}

int desktop_animation_step(void)
{
    if (desktop_animation == DESKTOP_ANIMATION_NONE) {
        return 0;
    }
    if (desktop_animation_frame < DESKTOP_ANIMATION_STEPS) {
        ++desktop_animation_frame;
    }
    if (desktop_animation_frame == DESKTOP_ANIMATION_STEPS) {
        if (desktop_animation == DESKTOP_ANIMATION_MENU_CLOSE) {
            desktop_menu_visible = 0;
        } else if (desktop_animation == DESKTOP_ANIMATION_WINDOW_CLOSE) {
            desktop_closing_window_visible = 0;
        }
        desktop_animation = DESKTOP_ANIMATION_NONE;
        desktop_animation_frame = 0;
    }
    return 1;
}

void desktop_draw(uint64_t usable_memory_mib)
{
    if (active_framebuffer == NULL) {
        return;
    }

    const uint64_t width = active_framebuffer->width;
    const uint64_t height = active_framebuffer->height;
    const struct desktop_wallpaper *wallpaper =
        &desktop_wallpapers[desktop_selected_wallpaper];

    framebuffer_clear(wallpaper->top);
    const uint64_t gradient_steps = 24;
    for (uint64_t step = 0; step < gradient_steps; ++step) {
        const uint64_t y0 = height * step / gradient_steps;
        const uint64_t y1 = height * (step + 1) / gradient_steps;
        const uint32_t mix = (uint32_t)(step * 255 / gradient_steps);
        uint32_t color = 0;
        for (uint8_t shift = 0; shift <= 16; shift += 8) {
            const uint32_t start = (wallpaper->top >> shift) & 0xff;
            const uint32_t end = (wallpaper->bottom >> shift) & 0xff;
            const uint32_t component =
                (start * (255 - mix) + end * mix) / 255;
            color |= component << shift;
        }
        framebuffer_rect(0, y0, width, y1 - y0, color);
    }

    const uint64_t motif_x = width * 3 / 5;
    const uint64_t motif_y = height / 5;
    const uint64_t motif_height =
        height > motif_y + 54 ? height - motif_y - 54 : 0;
    framebuffer_rect(motif_x, motif_y, width - motif_x,
                     motif_height, wallpaper->motif);
    framebuffer_rect(motif_x + (width - motif_x) / 4, motif_y,
                     (width - motif_x) * 3 / 4,
                     motif_height, wallpaper->motif_light);
    framebuffer_rect(0, 0, width, 2, wallpaper->accent);

    if (!desktop_initialized) {
        desktop_window_init();
    }

    desktop_window_render_all();

    const uint64_t taskbar_height = 54;
    framebuffer_rect(0, height - taskbar_height, width, taskbar_height,
                     DESKTOP_TASKBAR);
    framebuffer_rect(0, height - taskbar_height, width, 1, 0x344158);
    desktop_draw_start_button(width);

    const uint64_t status_x = width > 200 ? width - 104 : 0;
    framebuffer_text(status_x, height - 32, "READY",
                     0xc5d0e2, DESKTOP_TASKBAR, 1);

    const uint64_t app_x = 120;
    const uint64_t app_y = height - 38;
    framebuffer_rect(app_x, app_y, 72, 24, 0x202b3e);
    framebuffer_rect(app_x + 4, app_y + 4, 2, 16, DESKTOP_ACCENT);
    framebuffer_text(app_x + 12, app_y + 9, "TERMINAL",
                     0xe5ebf5, 0x202b3e, 1);
    framebuffer_rect(app_x + 88, app_y, 64, 24, 0x202b3e);
    framebuffer_rect(app_x + 92, app_y + 4, 2, 16, 0x68c5a5);
    framebuffer_text(app_x + 100, app_y + 9, "FILES",
                     0xe5ebf5, 0x202b3e, 1);
    framebuffer_rect(app_x + 166, app_y, 72, 24, 0x202b3e);
    framebuffer_rect(app_x + 170, app_y + 4, 2, 16, 0xb898ff);
    framebuffer_text(app_x + 174, app_y + 9, "SETTINGS",
                     0xe5ebf5, 0x202b3e, 1);

    framebuffer_text(18, 12, "DEVOS", 0xe8efff, wallpaper->top, 1);
    framebuffer_text(width > 400 ? width - 120 : 12, 12,
                     "DEVELOPER DESKTOP", 0xd6e1f2,
                     wallpaper->top, 1);
    desktop_draw_memory_text(usable_memory_mib);
    desktop_start_menu_draw();
    desktop_has_drawn = 1;
}

static void terminal_render_line(uint16_t row)
{
    if (active_framebuffer == NULL || !terminal_active) {
        return;
    }

    const uint64_t visible_columns =
        active_framebuffer->width > 24
            ? (active_framebuffer->width - 24) / 6
            : 0;
    const uint64_t columns = terminal_columns < visible_columns
                                 ? terminal_columns
                                 : visible_columns;
    const uint64_t y = 44 + (uint64_t)row * 9;
    if (row >= terminal_rows || y + 7 > active_framebuffer->height) {
        return;
    }

    char line[161];
    for (uint64_t column = 0; column < columns; ++column) {
        line[column] = terminal_cells[row][column];
    }
    line[columns] = '\0';
    framebuffer_text(12, y, line, 0xd6deeb, 0x10131a, 1);
}

static void terminal_render(void)
{
    if (active_framebuffer == NULL || !terminal_active) {
        return;
    }

    framebuffer_clear(0x10131a);
    framebuffer_rect(0, 0, active_framebuffer->width, 36, 0x1b2333);
    framebuffer_text(16, 12, "DEVOS TERMINAL", 0x77a9ff, 0x1b2333, 1);
    for (uint16_t row = 0; row < terminal_rows; ++row) {
        terminal_render_line(row);
    }
}

void framebuffer_terminal_open(void)
{
    if (active_framebuffer == NULL) {
        return;
    }

    terminal_active = 1;
    terminal_columns = (uint16_t)((active_framebuffer->width > 24
                                       ? (active_framebuffer->width - 24) / 6
                                       : 0) < 160
                                      ? (active_framebuffer->width > 24
                                             ? (active_framebuffer->width - 24) / 6
                                             : 0)
                                      : 160);
    terminal_rows = (uint16_t)((active_framebuffer->height > 52
                                   ? (active_framebuffer->height - 52) / 9
                                   : 0) < 80
                                  ? (active_framebuffer->height > 52
                                         ? (active_framebuffer->height - 52) / 9
                                         : 0)
                                  : 80);
    framebuffer_terminal_clear();
}

void framebuffer_terminal_clear(void)
{
    terminal_column = 0;
    terminal_row = 0;
    for (uint16_t row = 0; row < 80; ++row) {
        for (uint16_t column = 0; column < 160; ++column) {
            terminal_cells[row][column] = ' ';
        }
    }
    terminal_render();
}

static void terminal_newline(void)
{
    terminal_column = 0;
    if (terminal_rows == 0) {
        return;
    }
    if (terminal_row + 1 < terminal_rows) {
        ++terminal_row;
        terminal_render_line(terminal_row);
        return;
    }

    for (uint16_t row = 1; row < terminal_rows; ++row) {
        for (uint16_t column = 0; column < terminal_columns; ++column) {
            terminal_cells[row - 1][column] = terminal_cells[row][column];
        }
    }
    for (uint16_t column = 0; column < terminal_columns; ++column) {
        terminal_cells[terminal_rows - 1][column] = ' ';
    }
    terminal_row = terminal_rows - 1;
    terminal_render();
}

void framebuffer_terminal_write(const char *text)
{
    if (text == NULL || !terminal_active) {
        return;
    }

    while (*text != '\0') {
        const char character = *text++;
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            terminal_newline();
            terminal_render();
            continue;
        }
        if (terminal_columns == 0 || terminal_rows == 0) {
            continue;
        }
        if (terminal_column >= terminal_columns) {
            terminal_newline();
        }
        terminal_cells[terminal_row][terminal_column++] =
            character >= 0x20 && character <= 0x7e ? character : '?';
        terminal_render_line(terminal_row);
    }
}

void framebuffer_terminal_backspace(void)
{
    if (!terminal_active || terminal_column == 0 || terminal_rows == 0) {
        return;
    }
    --terminal_column;
    terminal_cells[terminal_row][terminal_column] = ' ';
    terminal_render_line(terminal_row);
}
