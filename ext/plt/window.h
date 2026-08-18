#pragma once

#include "clipboard.h"

#include <std/str/view.h>
#include <std/sys/types.h>

namespace plt {
    struct DropTarget;
    struct InputSink;

    enum class RenderBackend : u8 {
        Wayland,
        Cocoa,
        Headless
    };

    struct RenderContext {
        RenderBackend backend;
        void* connection;
        void* window;
    };

    // The union of the wp_cursor_shape_device_v1 shapes and the public
    // NSCursor cursors, collapsed where both platforms mean the same thing
    // (pointer covers pointingHandCursor, grab covers openHandCursor, and so
    // on). A backend without a native cursor for a value substitutes the
    // closest one it has.
    enum class PointerIcon : u8 {
        Default,
        ContextMenu,
        Help,
        Pointer,
        Progress,
        Wait,
        Cell,
        Crosshair,
        Text,
        VerticalText,
        Alias,
        Copy,
        Move,
        NoDrop,
        NotAllowed,
        Grab,
        Grabbing,
        ResizeEast,
        ResizeNorth,
        ResizeNorthEast,
        ResizeNorthWest,
        ResizeSouth,
        ResizeSouthEast,
        ResizeSouthWest,
        ResizeWest,
        ResizeEastWest,
        ResizeNorthSouth,
        ResizeNorthEastSouthWest,
        ResizeNorthWestSouthEast,
        ResizeColumn,
        ResizeRow,
        AllScroll,
        ZoomIn,
        ZoomOut,
        DndAsk,
        ResizeAll,
        DisappearingItem
    };

    // Where requestShowAt() places the window. TopOfActiveScreen is the
    // quick-terminal placement: a rect on the screen the pointer is
    // currently on, sized and positioned by WindowOptions::quickGeometry
    // (default full visibleFrame width, top-aligned, 40% of its height -
    // the placement's original, unconfigurable shape). The backend
    // resolves the rect against the actual screen.
    enum class ShowPlacement : u8 {
        Centered,
        TopOfActiveScreen
    };

    // One component of a quickGeometry spec ("<W>x<H>+<X>+<Y>"): either
    // an absolute pixel count or a percentage (0..100) of the target
    // screen's visibleFrame extent along that axis. Resolved against a
    // live screen only by the backend that implements
    // ShowPlacement::TopOfActiveScreen (Cocoa today); parsing and range
    // validation live in lib/shitty/quick_geometry.{h,cpp} so the
    // grammar is unit-testable without a live NSScreen.
    struct QuickGeometryDim {
        bool percent = false;
        u32 value = 0;
    };

    // Parsed quickGeometry option. width/height are the window's size;
    // x/y are its offset from the visibleFrame's top-left corner. The
    // defaults reproduce ShowPlacement::TopOfActiveScreen's original,
    // unconfigurable shape: full width, top-aligned, 40% height.
    struct QuickGeometry {
        QuickGeometryDim width{.percent = true, .value = 100};
        QuickGeometryDim height{.percent = true, .value = 40};
        QuickGeometryDim x{.percent = false, .value = 0};
        QuickGeometryDim y{.percent = false, .value = 0};
    };

    struct WindowInfo {
        i32 x = 0;
        i32 y = 0;
        u32 width = 0;
        u32 height = 0;
        u32 screenPixelWidth = 0;
        u32 screenPixelHeight = 0;
        float contentScale = 1.0f;
        bool focused = false;
        bool iconified = false;
        bool maximized = false;
        bool fullscreen = false;
        bool tiled = false;
    };

    struct WindowEvents {
        virtual void close() = 0;
    };

    struct FrameCallback {
        // Returns true when a frame was submitted for presentation.
        virtual bool frame(const WindowInfo& info) = 0;
    };

    struct WindowOptions {
        stl::StringView appId = {};
        stl::StringView title = {};
        u32 width = 800;
        u32 height = 600;
        u32 minimumWidth = 1;
        u32 minimumHeight = 1;
        bool decorations = true;
        // The titlebar's color matches the background the window is
        // created with, instead of the system chrome color. Cocoa-only;
        // Wayland has no titlebar of its own to recolor.
        bool transparentTitlebar = false;
        // The quick-terminal window: floats above other windows and
        // fullscreen spaces, and starts hidden - the caller shows it
        // itself through requestShowAt() once a hotkey fires. Cocoa-only
        // today; Wayland has no global hotkey path to trigger it.
        bool quick = false;
        // The rect requestShowAt(TopOfActiveScreen) resolves against the
        // active screen. Only consumed where TopOfActiveScreen actually
        // computes a placement (Cocoa today); ignored elsewhere, same as
        // quick above.
        QuickGeometry quickGeometry{};
        // Corner radius of the quick-terminal window's content layer, in
        // points; 0 keeps square corners. Cocoa-only, applied at window
        // creation time - Wayland has no equivalent compositor hook for an
        // undecorated window and ignores it, same as transparentTitlebar.
        u16 quickCornerRadius = 0;
        InputSink* input = nullptr;
        WindowEvents* events = nullptr;
        FrameCallback* frame = nullptr;
        // Null leaves the window rejecting every drag.
        DropTarget* drop = nullptr;
        // Encoded image bytes (PNG) for the application icon; empty keeps
        // the platform default. Cocoa sets the Dock icon from it, Wayland
        // has no icon protocol and ignores it.
        stl::StringView icon = {};
        // The human-visible application name. Cocoa pushes it to Launch
        // Services so the menu bar of an unbundled binary shows it
        // instead of argv[0]; Wayland ignores it (appId serves the
        // shell). The Cmd-Tab switcher is beyond reach: its label comes
        // from the application bundle, which a bare executable lacks.
        stl::StringView appName = {};
    };

    struct Window {
        virtual void requestShow() = 0;
        // Hides the window without destroying it (orderOut: on Cocoa);
        // the counterpart requestShow()/requestShowAt() bring it back.
        virtual void requestHide() = 0;
        // Like requestShow(), but places the window per placement
        // instead of always centering it.
        virtual void requestShowAt(ShowPlacement placement) = 0;
        virtual void requestClose() = 0;
        virtual void requestFrame() = 0;

        virtual void requestTitle(stl::StringView title) = 0;
        virtual void requestAttention() = 0;
        virtual void requestRestore() = 0;
        virtual void requestIconify() = 0;
        virtual void requestMove(i32 x, i32 y) = 0;
        virtual void requestFocus() = 0;
        virtual void requestMaximized(bool maximized) = 0;
        virtual void requestFullscreen(bool fullscreen) = 0;
        // Rounds (radius > 0) or squares (radius == 0) the window's own
        // corners, in points - not the one-shot construction-time
        // WindowOptions::quickCornerRadius, but a live toggle: the quick
        // window's geometric fullscreen chord (ui_quick_hotkey.mm) calls
        // this to square the corners before expanding over the whole
        // screen and restore them after collapsing back, so a
        // "fullscreen" window never shows desktop through masked
        // corners. Cocoa-only, same scope as quickCornerRadius itself -
        // Wayland and the headless backend accept the call and do
        // nothing.
        virtual void requestCornerRadius(u16 radius) = 0;
        virtual void requestResize(u32 width, u32 height) = 0;
        virtual void requestMinimumSize(u32 width, u32 height) = 0;
        virtual void requestResizeUnit(u32 width, u32 height, u32 baseWidth, u32 baseHeight) = 0;

        // The primary selection. On macOS it maps to the Find pasteboard: the
        // platform has no primary selection, and the Find pasteboard is the
        // closest persistent per-application slot. Reads may therefore observe
        // search-field text.
        virtual Clipboard* primary() = 0;
        // The regular clipboard.
        virtual Clipboard* secondary() = 0;
        virtual void requestPointerIcon(PointerIcon icon) = 0;
        // Opens uri with the desktop's default handler for its scheme. The
        // launch is fire-and-forget: failures surface only in the desktop
        // environment, never back to the caller.
        virtual void requestOpenUri(stl::StringView uri) = 0;
        // Caret rectangle in surface pixels. Input methods position their
        // candidate window next to it (text-input-v3 cursor rectangle on
        // Wayland, firstRectForCharacterRange on macOS).
        virtual void requestTextInputRect(i32 x, i32 y, u32 width, u32 height) = 0;

        virtual WindowInfo info() const = 0;
        // True between a requestShow()/requestShowAt() and the next
        // requestHide() (or the window never having been shown). The
        // caller that owns show/hide state - toggling the quick-terminal
        // window on a hotkey, for instance - needs this to stay correct
        // even when something other than that caller hides the window,
        // such as hide-on-resign-key.
        virtual bool visible() const = 0;
        // True while the user is interactively resizing the window; a
        // renderer presents transaction-synchronously then and stays
        // asynchronous otherwise.
        virtual bool inLiveResize() const = 0;
        virtual RenderContext renderContext() const = 0;
    };
}
