// Независимый повтор: те же четыре выражения render_vk.cpp, посчитанные
// до T10 и после, и сверенные побайтно. Настоящие заголовки проекта.
#include "render_blend.h"
#include "render_push_constants.h"

#include <cstdio>
#include <cstdint>

// Копия того, что делает RendererImpl::packColor.
static u32 packColor(const Color& c) {
    return (u32)(c.red) | ((u32)(c.green) << 8) | ((u32)(c.blue) << 16);
}
// Копия того, что делает RendererImpl::backgroundOpacity после T10.
static u16 backgroundOpacity() { return 100; }

struct Clear { float r, g, b, a; };

// ДО T10
static Clear clearBefore(const Color& bg) {
    return { bg.red / 255.0f, bg.green / 255.0f, bg.blue / 255.0f, 1.0f };
}
static u32 paneBefore(const Color& bg) { return packColor(bg); }
static u32 seamBefore(const Color& ink) { return packColor(ink) | fillPassBit; }

// ПОСЛЕ T10 — дословно четыре изменённых выражения
static Clear clearAfter(const Color& bg) {
    const u8 clearAlpha = backgroundAlphaFromPercent(backgroundOpacity());
    const Color clearInk = premultiply(bg, clearAlpha);
    return { clearInk.red / 255.0f, clearInk.green / 255.0f, clearInk.blue / 255.0f, clearAlpha / 255.0f };
}
static u32 paneAfter(const Color& bg) { return packPaneBackground(packColor(bg), backgroundOpacity()); }
static u32 seamAfter(const Color& ink) { return packPaneBackground(packColor(ink), 100) | fillPassBit; }

int main() {
    // Перебор всего куба цветов, а не трёх образцов.
    long long mismatches = 0, checked = 0;
    for (int r = 0; r < 256; ++r)
      for (int g = 0; g < 256; ++g)
        for (int b = 0; b < 256; ++b) {
            const Color c{(u8)r, (u8)g, (u8)b};
            const Clear x = clearBefore(c), y = clearAfter(c);
            ++checked;
            if (x.r != y.r || x.g != y.g || x.b != y.b || x.a != y.a) ++mismatches;
            if (paneBefore(c) != paneAfter(c)) ++mismatches;
            if (seamBefore(c) != seamAfter(c)) ++mismatches;
        }
    const Color sample{200, 100, 40};
    const Clear s = clearAfter(sample);
    printf("clear %f %f %f alpha %f\n", (double)s.r, (double)s.g, (double)s.b, (double)s.a);
    printf("pane 0x%08x seam 0x%08x\n", paneAfter(sample), seamAfter(sample));
    printf("colours checked: %lld (all 2^24), expressions per colour: 3\n", checked);
    printf("mismatches vs pre-T10: %lld -> %s\n", mismatches,
           mismatches == 0 ? "IDENTICAL" : "DIVERGES");
    return mismatches == 0 ? 0 : 1;
}
