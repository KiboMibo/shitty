#pragma once

namespace stl {
    class ObjPool;
}

class Fontpack;
struct Application;
class CellExtraStore;
class Renderer;
struct Pty;
struct PtyEventSource;
class Vterm;

// Application wiring. Components copy the dependencies they need during
// creation; the composer only establishes the graph and its shared lifetime.
struct Composer {
    ~Composer() noexcept;

    stl::ObjPool* pool = nullptr;
    Application* application = nullptr;
    CellExtraStore* cellExtras = nullptr;
    Fontpack* fonts = nullptr;
    Renderer* renderer = nullptr;
    Pty* pty = nullptr;
    PtyEventSource* ptyEvents = nullptr;
    Vterm* vterm = nullptr;
};
