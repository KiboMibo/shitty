/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_coretext.h"

#if defined(HAVE_CORETEXT)
    #include "composer.h"
    #include "font_resolver.h"
    #include "grapheme.h"
    #include "utf8.h"

    #include <std/lib/buffer.h>
    #include <std/mem/obj_pool.h>
    #include <std/str/view.h>

    #include <CoreFoundation/CoreFoundation.h>
    #include <CoreGraphics/CoreGraphics.h>
    #include <CoreText/CoreText.h>

using namespace stl;

namespace {
    struct CoreTextFont final: public Font {
        CoreTextFont(CTFontRef font, FontKind kind, FontMetrics metrics, FontStyle synthetic);
        ~CoreTextFont() noexcept;

        FontGlyph glyph(const u32* codepoints, size_t count, u16 cells) override;
        bool covers(u32 codepoint) override;
        Font* synthesize(ObjPool& owner, FontStyle style) override;

        CFStringRef makeString(const u32* codepoints, size_t count);
        CTLineRef makeLine(CFStringRef string);
        bool inspectLine(CTLineRef line, bool& color);
        bool drawLine(CTLineRef line, bool color);

        CTFontRef font_;
        FontKind kind_;
        FontMetrics metrics_;
        bool syntheticBold_ = false;
        bool syntheticItalic_ = false;
        u16 canvasWidth_ = 0;
        Buffer characters_;
        Buffer bitmap_;
    };

    struct CoreTextFontResolver final: public FontResolver {
        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;

        CTFontRef resolve(const FontRequest& request);
        CTFontRef resolvePath(const FontRequest& request);
        CTFontRef resolveName(const FontRequest& request);
        CTFontRef applyStyle(CTFontRef font, FontStyle style, u16 pixels);
        bool matchesName(CTFontRef font, CFStringRef name);
        FontMetrics measure(CTFontRef font);
    };

    u16 roundPositive(CGFloat value) {
        if (value <= 0) {
            return 0;
        }
        if (value >= UINT16_MAX) {
            return UINT16_MAX;
        }
        return (u16)(value + 0.5);
    }

    u16 roundUpPositive(CGFloat value) {
        if (value <= 0) {
            return 0;
        }
        if (value >= UINT16_MAX) {
            return UINT16_MAX;
        }
        const u16 truncated = (u16)(value);
        return truncated + (truncated < value);
    }

    bool sameString(CFStringRef left, CFStringRef right) {
        return left != nullptr && right != nullptr && CFStringCompare(left, right, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
    }

    bool pathName(StringView name) {
        return name.memChr('/') || name.memChr('\\');
    }

    CFStringRef makeString(StringView value) {
        return CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)(value.data()), value.length(), kCFStringEncodingUTF8, false);
    }
}

CoreTextFont::CoreTextFont(CTFontRef font, FontKind kind, FontMetrics metrics, FontStyle synthetic)
    : font_(font)
    , kind_(kind)
    , metrics_(metrics)
    , syntheticBold_(synthetic == FontStyle::Bold || synthetic == FontStyle::BoldItalic)
    , syntheticItalic_(synthetic == FontStyle::Italic || synthetic == FontStyle::BoldItalic)
{
}

Font* CoreTextFont::synthesize(ObjPool& owner, FontStyle style) {
    CFRetain(font_);
    return owner.make<CoreTextFont>(font_, FontKind::Overlay, metrics_, style);
}

CoreTextFont::~CoreTextFont() noexcept {
    CFRelease(font_);
}

bool CoreTextFont::covers(u32 codepoint) {
    UniChar characters[2];
    CFIndex length = 0;
    if (codepoint <= 0xffff && (codepoint < 0xd800 || codepoint > 0xdfff)) {
        characters[length++] = (UniChar)(codepoint);
    } else if (codepoint <= 0x10ffff) {
        const u32 scalar = codepoint - 0x10000;
        characters[length++] = (UniChar)(0xd800 + (scalar >> 10));
        characters[length++] = (UniChar)(0xdc00 + (scalar & 0x3ff));
    } else {
        return false;
    }
    CGGlyph glyphs[2] = {};
    return CTFontGetGlyphsForCharacters(font_, characters, glyphs, length);
}

CFStringRef CoreTextFont::makeString(const u32* codepoints, size_t count) {
    characters_.reset();
    characters_.grow(2 * count * sizeof(UniChar));
    for (size_t index = 0; index < count; ++index) {
        const u32 codepoint = codepoints[index];
        if (codepoint <= 0xffff && (codepoint < 0xd800 || codepoint > 0xdfff)) {
            const UniChar value = (UniChar)(codepoint);
            characters_.append(&value, sizeof(value));
        } else if (codepoint <= 0x10ffff) {
            const u32 scalar = codepoint - 0x10000;
            const UniChar values[] = {
                (UniChar)(0xd800 + (scalar >> 10)),
                (UniChar)(0xdc00 + (scalar & 0x3ff)),
            };
            characters_.append(values, sizeof(values));
        }
    }
    return CFStringCreateWithCharacters(kCFAllocatorDefault, (const UniChar*)(characters_.data()), characters_.used() / sizeof(UniChar));
}

CTLineRef CoreTextFont::makeLine(CFStringRef string) {
    const void* keys[] = {
        kCTFontAttributeName,
        kCTForegroundColorFromContextAttributeName,
    };
    const void* values[] = {
        font_,
        kCFBooleanTrue,
    };
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (attributes == nullptr) {
        return nullptr;
    }
    CFAttributedStringRef attributed = CFAttributedStringCreate(kCFAllocatorDefault, string, attributes);
    CFRelease(attributes);
    if (attributed == nullptr) {
        return nullptr;
    }
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CFRelease(attributed);
    return line;
}

bool CoreTextFont::inspectLine(CTLineRef line, bool& color) {
    color = false;
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    const CFIndex runCount = CFArrayGetCount(runs);
    if (runCount == 0) {
        return false;
    }
    for (CFIndex runIndex = 0; runIndex < runCount; ++runIndex) {
        auto run = (CTRunRef)(CFArrayGetValueAtIndex(runs, runIndex));
        const CFIndex glyphCount = CTRunGetGlyphCount(run);
        if (glyphCount == 0) {
            return false;
        }
        const CGGlyph* glyphs = CTRunGetGlyphsPtr(run);
        if (glyphs != nullptr) {
            for (CFIndex glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
                if (glyphs[glyphIndex] == 0) {
                    return false;
                }
            }
        } else {
            for (CFIndex glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
                CGGlyph glyph = 0;
                CTRunGetGlyphs(run, {glyphIndex, 1}, &glyph);
                if (glyph == 0) {
                    return false;
                }
            }
        }
        CFDictionaryRef attributes = CTRunGetAttributes(run);
        auto font = (CTFontRef)(CFDictionaryGetValue(attributes, kCTFontAttributeName));
        if (font != nullptr && (CTFontGetSymbolicTraits(font) & kCTFontColorGlyphsTrait) != 0) {
            color = true;
        }
    }
    return true;
}

bool CoreTextFont::drawLine(CTLineRef line, bool color) {
    const size_t bytesPerPixel = color ? 4 : 1;
    const size_t stride = (size_t)(canvasWidth_)*bytesPerPixel;
    bitmap_.zero(stride * metrics_.height);

    CGColorSpaceRef colorSpace = color ? CGColorSpaceCreateDeviceRGB() : CGColorSpaceCreateDeviceGray();
    if (colorSpace == nullptr) {
        return false;
    }
    const CGBitmapInfo bitmapInfo = color ? (CGBitmapInfo)(kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big) : (CGBitmapInfo)(kCGImageAlphaNone);
    CGContextRef context = CGBitmapContextCreate(bitmap_.mutData(), canvasWidth_, metrics_.height, 8, stride, colorSpace, bitmapInfo);
    CGColorSpaceRelease(colorSpace);
    if (context == nullptr) {
        return false;
    }

    CGContextSetAllowsAntialiasing(context, true);
    CGContextSetShouldAntialias(context, true);
    CGContextSetAllowsFontSmoothing(context, false);
    CGContextSetShouldSmoothFonts(context, false);
    if (color) {
        CGContextSetRGBFillColor(context, 1, 1, 1, 1);
    } else {
        CGContextSetGrayFillColor(context, 1, 1);
    }
    if (syntheticBold_) {
        // Fake bold: fill and stroke, the stroke width scaled to the size.
        CGContextSetTextDrawingMode(context, kCGTextFillStroke);
        CGContextSetLineWidth(context, metrics_.height * 0.03);
        if (color) {
            CGContextSetRGBStrokeColor(context, 1, 1, 1, 1);
        } else {
            CGContextSetGrayStrokeColor(context, 1, 1);
        }
    }
    CGAffineTransform matrix = CGAffineTransformIdentity;
    if (syntheticItalic_) {
        // Fake italic: a horizontal shear of about 14 degrees.
        matrix.c = 0.25;
    }
    CGContextSetTextMatrix(context, matrix);
    CGContextSetTextPosition(context, 0, metrics_.height - metrics_.baseline);
    CTLineDraw(line, context);
    CGContextRelease(context);
    return true;
}

FontGlyph CoreTextFont::glyph(const u32* codepoints, size_t count, u16 cells) {
    if (count == 0 || cells == 0 || codepoints[0] == Missing_Glyph_Marker) {
        return {};
    }
    canvasWidth_ = (u16)((cells < 2 ? cells : 2) * metrics_.width);
    CFStringRef string = makeString(codepoints, count);
    if (string == nullptr) {
        return {};
    }
    CTLineRef line = makeLine(string);
    CFRelease(string);
    if (line == nullptr) {
        return {};
    }
    bool color = false;
    const bool success = inspectLine(line, color) && drawLine(line, color);
    CFRelease(line);
    if (!success) {
        return {};
    }
    return {
        .data = bitmap_.data(),
        .len = bitmap_.used(),
        .color = color,
    };
}

bool CoreTextFontResolver::matchesName(CTFontRef font, CFStringRef name) {
    CFStringRef family = CTFontCopyFamilyName(font);
    CFStringRef full = CTFontCopyFullName(font);
    CFStringRef postscript = CTFontCopyPostScriptName(font);
    const bool matches = sameString(name, family) || sameString(name, full) || sameString(name, postscript);
    if (family != nullptr) {
        CFRelease(family);
    }
    if (full != nullptr) {
        CFRelease(full);
    }
    if (postscript != nullptr) {
        CFRelease(postscript);
    }
    return matches;
}

CTFontRef CoreTextFontResolver::applyStyle(CTFontRef font, FontStyle style, u16 pixels) {
    CTFontSymbolicTraits traits = 0;
    if (style == FontStyle::Bold || style == FontStyle::BoldItalic) {
        traits |= kCTFontBoldTrait;
    }
    if (style == FontStyle::Italic || style == FontStyle::BoldItalic) {
        traits |= kCTFontItalicTrait;
    }
    if (traits == 0) {
        return font;
    }
    CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, pixels, nullptr, traits, traits);
    CFRelease(font);
    if (styled == nullptr || (CTFontGetSymbolicTraits(styled) & traits) != traits) {
        if (styled != nullptr) {
            CFRelease(styled);
        }
        return nullptr;
    }
    return styled;
}

CTFontRef CoreTextFontResolver::resolvePath(const FontRequest& request) {
    if (request.style != FontStyle::Regular) {
        return nullptr;
    }
    Buffer path(request.name);
    CGDataProviderRef provider = CGDataProviderCreateWithFilename(path.cStr());
    if (provider == nullptr) {
        return nullptr;
    }
    CGFontRef graphicsFont = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
    if (graphicsFont == nullptr) {
        return nullptr;
    }
    CTFontRef font = CTFontCreateWithGraphicsFont(graphicsFont, request.pixels, nullptr, nullptr);
    CGFontRelease(graphicsFont);
    return font;
}

CTFontRef CoreTextFontResolver::resolveName(const FontRequest& request) {
    if (request.name == StringView(u8"monospace")) {
        CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, request.pixels, nullptr);
        return font == nullptr ? nullptr : applyStyle(font, request.style, request.pixels);
    }

    CFStringRef name = makeString(request.name);
    if (name == nullptr) {
        return nullptr;
    }
    CTFontRef font = CTFontCreateWithName(name, request.pixels, nullptr);
    if (font != nullptr && !matchesName(font, name)) {
        CFRelease(font);
        font = nullptr;
    }
    CFRelease(name);
    return font == nullptr ? nullptr : applyStyle(font, request.style, request.pixels);
}

CTFontRef CoreTextFontResolver::resolve(const FontRequest& request) {
    return pathName(request.name) ? resolvePath(request) : resolveName(request);
}

FontMetrics CoreTextFontResolver::measure(CTFontRef font) {
    const UniChar character = 'M';
    CGGlyph glyph = 0;
    CGSize advance{};
    if (CTFontGetGlyphsForCharacters(font, &character, &glyph, 1) && glyph != 0) {
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyph, &advance, 1);
    }
    const CGFloat ascent = CTFontGetAscent(font);
    const CGFloat descent = CTFontGetDescent(font);
    const CGFloat leading = CTFontGetLeading(font);
    return {
        .width = roundPositive(advance.width),
        .height = roundUpPositive(ascent + descent + leading),
        .baseline = roundUpPositive(ascent),
    };
}

Font* CoreTextFontResolver::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    CTFontRef font = resolve(request);
    if (font == nullptr) {
        return nullptr;
    }
    const FontMetrics actual = measure(font);
    if (actual.width == 0 || actual.height == 0 || actual.baseline == 0) {
        CFRelease(font);
        return nullptr;
    }
    if (request.kind == FontKind::Primary) {
        metrics = actual;
    } else if (request.kind == FontKind::Overlay && (metrics.height != actual.height || metrics.baseline != actual.baseline)) {
        CFRelease(font);
        return nullptr;
    }
    return owner.make<CoreTextFont>(font, request.kind, metrics, FontStyle::Regular);
}

FontResolver* createCoreTextFontResolver(Composer& composer) {
    return composer.pool->make<CoreTextFontResolver>();
}
#else
FontResolver* createCoreTextFontResolver(Composer& composer) {
    (void)(composer);
    return nullptr;
}
#endif
