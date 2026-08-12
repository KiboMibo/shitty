#pragma once

#include "clipboard.h"

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

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

    // The compass edges an interactive client-side resize starts from.
    enum class WindowEdge : u8 {
        Top,
        Bottom,
        Left,
        Right,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    struct WindowLayerOptions {
        // Pointer input entering the layer's surface goes to this sink;
        // null routes it to the window's own sink. Keyboard focus always
        // belongs to the window.
        InputSink* input = nullptr;
    };

    // A composited child surface inside the window: its own buffers and
    // its own presentation cadence, positioned in window coordinates and
    // stacked above the window's surface by the system compositor.
    // Chrome and the terminal grid on separate layers keep either side's
    // damage out of the other's swapchain.
    struct WindowLayer {
        virtual RenderContext renderContext() const = 0;
        // The layer's top-left corner in window coordinates and its
        // logical size. The client sizes its own buffers; the geometry
        // maps them onto the window on the window's next commit.
        virtual void setGeometry(i32 x, i32 y, u32 width, u32 height) = 0;
        // A synchronized layer commits atomically with the window
        // surface - the mode for live resizes. Desynchronized is the
        // steady state, presenting at the layer's own pace.
        virtual void setSynchronized(bool synchronized) = 0;
    };

    // Reactions to the native title-bar tab strip, delivered on the
    // event loop like every other window event.
    struct TabStripEvents {
        virtual void tabSelected(size_t index) = 0;
        virtual void tabOpened() = 0;
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

        // Project a tab strip into the native title bar: one title per
        // tab and the active index. A backend without a native title bar
        // (Wayland, headless) ignores the call; zero count removes the
        // strip. The titles are copied before returning.
        virtual void setTabStrip(const stl::StringView* titles, size_t count, size_t active, TabStripEvents* events) = 0;
        // Replace the sink window input is delivered to. A chrome that
        // owns the window surface installs itself here and forwards what
        // it does not consume; layers carry their own sinks.
        virtual void setInput(InputSink* sink) = 0;
        // The layer lives in owner, which must not outlive the window.
        virtual WindowLayer* createLayer(stl::ObjPool& owner, const WindowLayerOptions& options) = 0;
        // Hand the window to the window system for an interactive move
        // or resize begun from client-side chrome. Wayland forwards the
        // grab to the compositor; Cocoa moves borderless windows itself,
        // so its resize is a no-op.
        virtual void startInteractiveMove() = 0;
        virtual void startInteractiveResize(WindowEdge edge) = 0;

        virtual WindowInfo info() const = 0;
        // True while the user is interactively resizing the window; a
        // renderer presents transaction-synchronously then and stays
        // asynchronous otherwise.
        virtual bool inLiveResize() const = 0;
        virtual RenderContext renderContext() const = 0;
    };
}
