/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */
/* The C embedding facade over the shitty VT core: one opaque terminal,
 * fed bytes, read back as a grid. Everything the terminal wants from
 * its surroundings arrives through shitty_vt_callbacks; everything it
 * emits toward the child sits in the reply buffer until the embedder
 * drains it into its own pty. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct shitty_vt shitty_vt;

/* shitty_vt_cell.attributes bits */
#define SHITTY_VT_ATTR_BOLD (1u << 0)
#define SHITTY_VT_ATTR_FAINT (1u << 1)
#define SHITTY_VT_ATTR_ITALIC (1u << 2)
#define SHITTY_VT_ATTR_BLINK (1u << 3)
#define SHITTY_VT_ATTR_INVERSE (1u << 4)
#define SHITTY_VT_ATTR_CONCEAL (1u << 5)
#define SHITTY_VT_ATTR_STRIKE (1u << 6)
#define SHITTY_VT_ATTR_OVERLINE (1u << 7)

/* shitty_vt_modes() bits */
#define SHITTY_VT_MODE_ALT_SCREEN (1u << 0)
#define SHITTY_VT_MODE_BRACKETED_PASTE (1u << 1)
#define SHITTY_VT_MODE_APP_CURSOR_KEYS (1u << 2)
#define SHITTY_VT_MODE_APP_KEYPAD (1u << 3)
#define SHITTY_VT_MODE_FOCUS_EVENTS (1u << 4)
#define SHITTY_VT_MODE_AUTO_WRAP (1u << 5)
#define SHITTY_VT_MODE_ORIGIN (1u << 6)
#define SHITTY_VT_MODE_INSERT (1u << 7)
#define SHITTY_VT_MODE_CURSOR_VISIBLE (1u << 8)
#define SHITTY_VT_MODE_SCREEN_REVERSE (1u << 9)
#define SHITTY_VT_MODE_SYNCHRONIZED_OUTPUT (1u << 10)
#define SHITTY_VT_MODE_MOUSE_CLICK (1u << 11)
#define SHITTY_VT_MODE_MOUSE_DRAG (1u << 12)
#define SHITTY_VT_MODE_MOUSE_MOTION (1u << 13)
#define SHITTY_VT_MODE_MOUSE_SGR (1u << 14)
/* DECSET 1007: while the alternate screen is up, wheel input should be
 * sent as arrow keys rather than scrolling a history it does not keep. */
#define SHITTY_VT_MODE_ALTERNATE_SCROLL (1u << 15)

    /* One readable cell. Colors are resolved through the palette into
 * 0x00BBGGRR - the little-endian view of struct { uint8_t r, g, b; }.
 * The grapheme is the cell's decoded codepoints; an empty cell has
 * grapheme_len 0. The pointer is valid only for the duration of the
 * shitty_vt_each_cell callback. */
    typedef struct shitty_vt_cell {
        const uint32_t* grapheme;
        size_t grapheme_len;
        uint32_t foreground;
        uint32_t background;
        uint32_t underline_color;
        uint16_t attributes;
        /* 0 none, 1 straight, 2 double, 3 curly, 4 dotted, 5 dashed */
        uint8_t underline_style;
        /* 1 or 2; the continuation of a wide cell is not reported */
        uint8_t width;
    } shitty_vt_cell;

    typedef struct shitty_vt_cursor {
        uint16_t column;
        uint16_t row;
        /* 0 hidden, 1 filled block, 2 hollow block, 3 underline, 4 bar */
        uint8_t style;
        uint8_t visible;
    } shitty_vt_cursor;

    typedef void (*shitty_vt_cell_fn)(void* user, uint16_t row, uint16_t column, const shitty_vt_cell* cell);

    /* Everything the terminal may want from its embedder. Every callback
 * may be null; user is passed back verbatim. The terminal keeps the
 * pointer, not a copy - the struct must outlive it. */
    typedef struct shitty_vt_callbacks {
        void* user;
        /* The application published a new title. */
        void (*title_changed)(void* user, const uint8_t* title, size_t len);
        /* BEL, or an attention request - present it however fits. */
        void (*bell)(void* user);
        /* The presentation moved; re-read the grid when convenient. */
        void (*damaged)(void* user);
        /* A hyperlink wants opening on the embedder's desktop. */
        void (*open_uri)(void* user, const uint8_t* uri, size_t len);
        /* OSC 52: the application replaced a selection. 0 is the primary
     * selection, 1 the clipboard. */
        void (*clipboard_set)(void* user, int clipboard, const uint8_t* bytes, size_t len);
        /* XTWINOPS asked for a grid this large; the embedder decides and
     * answers with shitty_vt_resize if it agrees. */
        void (*resize_request)(void* user, uint16_t columns, uint16_t rows);
    } shitty_vt_callbacks;

    /* callbacks may be null when the embedder wants none. */
    shitty_vt* shitty_vt_new(uint16_t columns, uint16_t rows, uint16_t save_lines, const shitty_vt_callbacks* callbacks);
    void shitty_vt_free(shitty_vt*);
    void shitty_vt_feed(shitty_vt*, const uint8_t* bytes, size_t len);
    void shitty_vt_resize(shitty_vt*, uint16_t columns, uint16_t rows);

    /* Terminal-generated replies (DA, DSR, ...) the embedder must forward
 * to its pty. Drains up to cap bytes into out and returns how many. */
    size_t shitty_vt_take_replies(shitty_vt*, uint8_t* out, size_t cap);

    /* Walks the visible grid row-major; wide-cell continuations are
 * skipped. */
    void shitty_vt_each_cell(shitty_vt*, shitty_vt_cell_fn, void* user);
    shitty_vt_cursor shitty_vt_cursor_state(const shitty_vt*);
    uint32_t shitty_vt_modes(const shitty_vt*);

#ifdef __cplusplus
}
#endif
