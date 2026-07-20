#pragma once

namespace stl {
    class ObjPool;
}

class Fontpack;
class Renderer;
class Vterm;

// Application wiring. Components copy the dependencies they need during
// creation; the composer only establishes the graph and its shared lifetime.
struct Composer {
    stl::ObjPool* pool = nullptr;
    Fontpack* fonts = nullptr;
    Renderer* renderer = nullptr;
    Vterm* vterm = nullptr;
};
