/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "rect.h"
#include "terminal_types.h"

#include <plt/input.h>

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>
#include <stdint.h>

namespace stl {
    class Input;
    class ObjPool;
    class Output;
}

struct Composer;
struct PtyHandle;
struct CellExtraStore;
struct Screen;
struct VtermTraceFactory;
struct Vterm;

struct VtermTitleChanged {
    Vterm* source;
    stl::StringView title;
};

// A8: what one terminal is given instead of reading the window. The grid
// is the pane's own - the number of cells it holds, which is what every
// margin, erase, cursor clamp and CSI report inside Vterm means by
// "columns" and "rows" - and the origin is where the pane's content
// begins inside the window's content box, in backing pixels.
//
// The origin is not an inset and is never merged into one: the window's
// insets are the border option plus what chrome reserves on each side
// (A1), owned by Composer, while the origin is the layout's, one value
// per pane. Everything downstream adds them; nothing stores them added.
//
// While a window shows one terminal the origin is (0, 0) and the grid is
// the window's, which is why the whole of T7 changes no visible
// behaviour.
struct PaneGeometry {
    u16 columns = 0;
    u16 rows = 0;
    i32 originX = 0;
    i32 originY = 0;
};

enum class VtModifier : u8 {
    none = 0,
    shift = 1,
    control = 2,
    shift_control = 3,
    alt = 4,
    shift_alt = 5,
    control_alt = 6,
    shift_control_alt = 7,
    super = 8
};

constexpr VtModifier operator|(VtModifier lhs, VtModifier rhs) {
    return (VtModifier)((u8)(lhs) | (u8)(rhs));
}

constexpr VtModifier operator&(VtModifier lhs, VtModifier rhs) {
    return (VtModifier)((u8)(lhs) & (u8)(rhs));
}

enum class MouseTrackingMode : u8 {
    Disabled = 0,
    X10_Compat,
    VT200,
    VT200_ButtonEvent,
    VT200_AnyEvent,
    VT200_Highlight
};
enum class MouseTrackingEnc : u8 {
    Default = 0,
    UTF8,
    SGR,
    URXVT,
    SGRPixels
};

struct MouseTrackingState {
    MouseTrackingMode mode = MouseTrackingMode::Disabled;
    MouseTrackingEnc enc = MouseTrackingEnc::Default;
    bool focusEventMode = false;
    u32 generation = 0;

    void setMode(MouseTrackingMode value);
    void setEncoding(MouseTrackingEnc value);
};

struct RectangleOrigin {
    u16 rowBase;
    u16 columnBase;
    u16 rowLimit;
    u16 columnLimit;
};

enum class VtermKeyEventType : u8 {
    Press = 1,
    Repeat = 2,
    Release = 3
};

struct VtermTextResult {
    stl::StringView text;
    bool status = false;
};

struct VtermState {
    bool synchronizedOutput = false;
};

struct TerminalUpdate {
    // The damaged view rows, ascending; each re-renders wholly.
    const TerminalRow* rows = nullptr;
    size_t rowCount = 0;
    // A9: the grid of the pane this frame belongs to, which is the grid
    // its cells were allocated and indexed by - not the window's. Zero
    // is a refused frame, not a window-sized default: a renderer that
    // reads zero here returns false, the way it already refuses a null
    // colors. The default is absent on purpose, because a field everyone
    // filled in with the window would hide the very gap it marks.
    u16 gridColumns = 0; // width of TerminalRow::cells and its indexing stride
    u16 gridRows = 0;    // height of the grid row.row indexes into
    // The shaping canvas of this frame: a strip-consuming renderer pulls
    // Screen::rowSpans and the strip arenas through it. Null when the
    // update is synthesized without a screen (renderer-internal repaints).
    Screen* shapes = nullptr;
    // The preedit preview: overlayCount cells drawn over overlayRow from
    // overlayColumn, covering the row content beneath them. They exist
    // outside the screen model, so the renderer shapes them itself via
    // shapes->shapeCells. Zero count when no preview is active.
    const TerminalCell* overlayCells = nullptr;
    u16 overlayRow = 0;
    u16 overlayColumn = 0;
    u16 overlayCount = 0;
    // Every row carries cells foreign to the shaping screen (retained
    // cells re-rendered through another fontpack); the renderer shapes
    // each row via shapes->shapeCells instead of the screen rows.
    bool shapeFromCells = false;
    const TerminalColors* colors = nullptr;
    u32 viewOffset = 0;
    u32 historyRows = 0;
    TerminalCursor cursor;
    Rect selection;
    Rect snappedSelection;
    Color selectionForeground;
    Color selectionBackground;
    u8 selectionColorMask = 0;
    u32 hoveredHyperlink = 0;
    u32 hoveredLinkBegin = 0;
    u32 hoveredLinkEnd = 0;
    bool screenReverse = false;
    bool blinkVisible = true;
    bool cursorBlink = false;
};

struct Vterm {
    // A5: "visible" and "focused" are two states, not one. This is
    // visibility - the terminal is a pane of the tab on screen, so it
    // renders and wakes frames. It says nothing about input: a tab shows
    // many panes and exactly one of them is focused, and inventing focus
    // here would flicker a lie at a child watching for the events.
    //
    // Makes the terminal's presentation current and repaints it. The
    // repaint is not optional: a renderer may retain cells from the
    // presentation it consumed before this one.
    virtual void show() = 0;
    // A5: off screen - the tab went to the background, or the pane was
    // closed. Ends the terminal's current presentation and drops its
    // input focus with it, since a terminal nobody can see cannot be the
    // one taking input. The converse does not hold: losing the focus to
    // a neighbouring pane leaves this one visible.
    virtual void hide() = 0;
    // Input callbacks are invoked by whichever client currently presents
    // this terminal; Vterm does not join a global input router itself.
    virtual bool key(const plt::KeyInput& input) = 0;
    virtual bool text(const plt::TextInput& input) = 0;
    virtual bool pointerMotion(const plt::PointerMotionInput& input) = 0;
    virtual bool pointerButton(const plt::PointerButtonInput& input) = 0;
    virtual bool scroll(const plt::ScrollInput& input) = 0;
    virtual void focus(bool focused) = 0;
    virtual void pointerPresence(bool present) = 0;
    virtual void flush() = 0;
    // Terminal actions are likewise invoked by the presenting client.
    virtual void copy() = 0;
    virtual void paste(bool primary) = 0;
    virtual void pageUp() = 0;
    virtual void pageDown() = 0;
    // What Ctrl+L means, reached from a platform's own chord. The byte
    // goes to the shell rather than clearing here, so the shell's own
    // idea of a clear - prompt redraw and all - is what happens.
    virtual void clear() = 0;
    virtual void feedPty(stl::StringView bytes) = 0;
    // One batch, one round of cursor and presentation bookkeeping: the
    // pty drain hands over whole blocks, and paying the per-feed wrap
    // per block would cost more than the parse.
    virtual void feedPty(const stl::StringView* slices, size_t count) = 0;
    virtual void expose() = 0;
    virtual void sendBytes(stl::StringView bytes, bool userInput) = 0;
    // Input-method composition preview, rendered as an overlay on the
    // cursor row of the emitted frame; never enters the screen model,
    // the scrollback, or the pty. Empty text clears the preview.
    // cursorBegin/cursorEnd are byte offsets into text, or -1 when the
    // input method hides its cursor.
    virtual void preedit(stl::StringView text, i32 cursorBegin, i32 cursorEnd) = 0;
    // Text dropped onto the window by a drag-and-drop session; the stream
    // is pulled on the calling fiber chunk by chunk under the PTY mutex,
    // with the same sanitizing and bracketed-paste treatment as a
    // clipboard paste. Off fibers the payload is buffered whole (bounded)
    // and replayed from a transaction.
    virtual void dropText(stl::Input& source) = 0;
    // A text/uri-list drop: every entry is inserted shell-quoted with a
    // trailing separator through the same paste path as dropText().
    virtual void dropUriList(stl::Input& source) = 0;

    virtual bool expireSynchronizedOutput(bool force) = 0;
    virtual bool advanceAnimation(bool force) = 0;
    virtual const TerminalUpdate* output() = 0;
    virtual void consume() = 0;
    virtual VtermState state() const = 0;

    // A8: this pane's geometry changed: adopt it and redraw. Delivered to
    // every session, background ones included - a terminal that resized
    // only on activation would come back wrong.
    //
    // One call, not a setter plus a trigger: the grid rebuild reflows the
    // scrollback and reports CSI 48 to the child, so a second idle pass
    // over it would send the shell a phantom resize report it never asked
    // for. There is no way to run it twice by accident when running it is
    // the only thing this does.
    virtual void paneResized(const PaneGeometry& geometry) = 0;
    // The font pack was replaced: every metric is new; rebuild and redraw.
    virtual void fontChanged() = 0;
    // Whether the presentation moved past what the renderer last
    // consumed.
    virtual bool presentationChanged() const = 0;
    // A11: the cells this terminal holds - its primary screen and, once
    // it has one, its alternate. A store shared by the whole window is
    // sized by the sum of this over every live pane, which is the only
    // number that stays right when the panes are of different sizes.
    virtual size_t cellCapacity() const = 0;

    // The terminal and everything it owns - fiber stacks, screens - come
    // out of owner, which is what lets a session die by dropping its
    // arena.
    //
    // A8: the geometry is a parameter rather than something read off the
    // composer, because the very first screen is already sized to it. A
    // terminal that read the window at birth and only accepted a pane
    // afterwards would allocate the window's grid, reflow it once, and
    // hand its child a resize it never asked for.
    static Vterm* create(stl::ObjPool& owner, Composer& composer, const PaneGeometry& geometry, PtyHandle& pty, VtermTraceFactory* traceFactory);
};
