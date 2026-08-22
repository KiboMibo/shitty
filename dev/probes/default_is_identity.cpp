// R10-qa. Умолчание backgroundOpacity = 100 обязано быть тождеством:
// при нём T10 не имеет права изменить ни одного байта, который проект
// писал до неё. Проверка исчерпывающая, а не на выборке.
#include <cstdio>
#include <cstdint>

// типы приходят из libstd
#if 0
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#include "../lib/shitty/render_blend.h"

// Формула, которой проект пользовался до T10 (render_reference.cpp:298).
static u8 legacyMix(u8 foreground, u8 background, u8 coverage) {
    return (u8)(((unsigned)(foreground)*coverage + (unsigned)(background) * (255 - coverage) + 127) / 255);
}

int main() {
    if (backgroundAlphaFromPercent(100) != 255) {
        std::printf("FAIL: backgroundAlphaFromPercent(100) = %u\n", backgroundAlphaFromPercent(100));
        return 1;
    }
    for (u32 channel = 0; channel < 256; ++channel) {
        if (premultiplyChannel((u8)channel, 255) != (u8)channel) {
            std::printf("FAIL: premultiplyChannel(%u,255) = %u\n", channel, premultiplyChannel((u8)channel, 255));
            return 1;
        }
    }
    unsigned long long checked = 0;
    for (u32 fg = 0; fg < 256; ++fg) {
        for (u32 bg = 0; bg < 256; ++bg) {
            for (u32 cov = 0; cov < 256; ++cov) {
                const BlendedPixel pixel = blendOverBackground(
                    {(u8)fg, (u8)fg, (u8)fg}, {(u8)bg, (u8)bg, (u8)bg}, (u8)cov, 255);
                const u8 legacy = legacyMix((u8)fg, (u8)bg, (u8)cov);
                if (pixel.color.red != legacy || pixel.alpha != 255) {
                    std::printf("FAIL: fg=%u bg=%u cov=%u -> %u/%u, legacy %u\n",
                                fg, bg, cov, pixel.color.red, pixel.alpha, legacy);
                    return 1;
                }
                ++checked;
            }
        }
    }
    std::printf("IDENTICAL at the default: %llu triples, alpha 255 throughout\n", checked);
    // И контроль: при 50 оно обязано отличаться, иначе тест зелен на всём.
    const BlendedPixel half = blendOverBackground({0,0,0}, {255,255,255}, 128, backgroundAlphaFromPercent(50));
    const u8 legacyHalf = legacyMix(0, 255, 128);
    std::printf("control at 50: premultiplied %u vs unpremultiplied %u (must differ)\n", half.color.red, legacyHalf);
    return half.color.red == legacyHalf ? 1 : 0;
}
