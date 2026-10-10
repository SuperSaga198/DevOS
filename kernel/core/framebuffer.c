#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>

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

static void card(uint64_t x, uint64_t y, uint64_t width, uint64_t height,
                 const char *title, const char *value, const char *detail,
                 uint32_t accent)
{
    const uint64_t margin = 24;
    if (width < margin * 2 || height < 116) {
        return;
    }
    framebuffer_rect(x, y, width, height, 0x1b2333);
    framebuffer_rect(x, y, 3, height, accent);
    framebuffer_text(x + margin, y + 20, title, 0xa7b1c2, 0x1b2333, 2);
    framebuffer_text(x + margin, y + 52, value, 0xf3f6fc, 0x1b2333, 3);
    framebuffer_text(x + margin, y + height - 25, detail, 0x91a0b5,
                     0x1b2333, 1);
}

void desktop_draw(uint64_t usable_memory_mib)
{
    if (active_framebuffer == NULL) {
        return;
    }

    const uint64_t width = active_framebuffer->width;
    const uint64_t height = active_framebuffer->height;
    framebuffer_clear(0x101521);

    const uint64_t sidebar_width = width >= 900 ? 180 : 0;
    const uint64_t top_height = 48;
    framebuffer_rect(0, 0, width, top_height, 0x171e2b);
    framebuffer_text(18, 16, "DEVOS", 0x77a9ff, 0x171e2b, 2);
    framebuffer_text(width > 430 ? width - 190 : 12, 18,
                     "DEVELOPER PREVIEW", 0xa7b1c2, 0x171e2b, 1);

    if (sidebar_width != 0) {
        framebuffer_rect(0, top_height, sidebar_width, height - top_height,
                        0x141b28);
        framebuffer_text(22, 82, "> HOME", 0xf3f6fc, 0x141b28, 2);
        framebuffer_text(22, 126, "TOOLS", 0x91a0b5, 0x141b28, 2);
        framebuffer_text(22, 164, "TERMINAL", 0x91a0b5, 0x141b28, 2);
        framebuffer_text(22, 202, "SETTINGS", 0x91a0b5, 0x141b28, 2);
    }

    const uint64_t left = sidebar_width + (sidebar_width ? 32 : 16);
    const uint64_t right = width > 16 ? width - 16 : 0;
    const uint64_t content_width = right > left ? right - left : 0;
    const uint64_t content_top = 78;

    framebuffer_text(left, content_top, "GOOD MORNING", 0xa7b1c2, 0x101521, 2);
    framebuffer_text(left, content_top + 28, "BUILD SOMETHING GREAT",
                     0xf3f6fc, 0x101521, 3);

    if (content_width >= 250 && height > 310) {
        const uint64_t gap = 16;
        const uint64_t card_width = (content_width - gap) / 2;
        const uint64_t card_height = 134;
        const uint64_t first_y = content_top + 88;
        card(left, first_y, card_width, card_height, "SYSTEM MEMORY",
             usable_memory_mib ? "READY" : "UNKNOWN",
             "USABLE MEMORY DETECTED", 0x5685ef);
        card(left + card_width + gap, first_y, card_width, card_height,
             "KERNEL", "X86-64", "FREESTANDING PREVIEW", 0x48c9a0);
        card(left, first_y + card_height + gap, card_width, card_height,
             "NETWORK", "NOT READY", "DRIVER NOT IMPLEMENTED", 0xf2a65a);
        card(left + card_width + gap, first_y + card_height + gap,
             card_width, card_height, "AUDIO", "NOT READY",
             "DRIVER NOT IMPLEMENTED", 0xe37da8);
    }

    framebuffer_text(left, height > 24 ? height - 20 : 0,
                     "DEVOS  -  EXPERIMENTAL BUILD", 0x748198, 0x101521, 1);
    if (height > 60) {
        framebuffer_text(left, height - 44, "PRESS T TO OPEN TERMINAL",
                         0x77a9ff, 0x101521, 1);
    }
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
