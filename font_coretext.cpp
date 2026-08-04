/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_coretext.h"

#if defined(HAVE_CORETEXT)
    #include "composer.h"
    #include "font_face.h"
    #include "font_resolver.h"
    #include "grapheme.h"
    #include "options.h"
    #include "utf8.h"

    #include <std/ios/sys.h>
    #include <std/lib/buffer.h>
    #include <std/mem/obj_pool.h>
    #include <std/str/view.h>
    #include <std/sys/throw.h>

    #include <CoreFoundation/CoreFoundation.h>
    #include <CoreGraphics/CoreGraphics.h>
    #include <CoreText/CoreText.h>

using namespace stl;

namespace {
    struct CoreTextFont final: public Font {
        CoreTextFont(IntrusivePtr<FontFace> source, CTFontRef font, FontKind kind, FontMetrics metrics, FontStyle synthetic);
        ~CoreTextFont() noexcept;

        void render(const u32* codepoints, size_t count, u16 cells, void* buf) override;
        bool covers(u32 codepoint) override;
        bool colored() const override;
        Font* synthesize(ObjPool& owner, FontStyle style) override;
        FontFace* face() override;

        CFStringRef makeString(const u32* codepoints, size_t count);
        CTLineRef makeLine(CFStringRef string);
        bool inspectLine(CTLineRef line, bool& color);
        bool drawLine(CTLineRef line, bool color);

        IntrusivePtr<FontFace> source_;
        CTFontRef font_;
        FontKind kind_;
        FontMetrics metrics_;
        bool syntheticBold_ = false;
        bool syntheticItalic_ = false;
        u16 canvasWidth_ = 0;
        Buffer characters_;
        Buffer runScratch_;
        Buffer columns_;
        Buffer bitmap_;
    };

    struct CoreTextFontResolver final: public FontResolver {
        FontFace* resolve(const FontRequest& request) override;

        CTFontRef resolveName(const FontRequest& request);
        CTFontRef applyStyle(CTFontRef font, FontStyle style, u16 pixels);
        bool matchesName(CTFontRef font, CFStringRef name);
        FontFace* extractFace(CTFontRef font);
    };

    struct CoreTextFontRenderer final: public FontRenderer {
        Font* render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) override;

        CTFontRef openFace(const FontFace& face, u16 pixels);
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

CoreTextFont::CoreTextFont(IntrusivePtr<FontFace> source, CTFontRef font, FontKind kind, FontMetrics metrics, FontStyle synthetic)
    : source_(source)
    , font_(font)
    , kind_(kind)
    , metrics_(metrics)
    , syntheticBold_(synthetic == FontStyle::Bold || synthetic == FontStyle::BoldItalic)
    , syntheticItalic_(synthetic == FontStyle::Italic || synthetic == FontStyle::BoldItalic)
{
}

FontFace* CoreTextFont::face() {
    return source_.mutPtr();
}

Font* CoreTextFont::synthesize(ObjPool& owner, FontStyle style) {
    CFRetain(font_);
    return owner.make<CoreTextFont>(source_, font_, FontKind::Overlay, metrics_, style);
}

CoreTextFont::~CoreTextFont() noexcept {
    CFRelease(font_);
}

void CoreTextFont::render(const u32* codepoints, size_t count, u16 cells, void* buf) {
    const bool color = colored();
    const size_t bytesPerPixel = color ? 4 : 1;
    const size_t stride = (size_t)(cells)*metrics_.width * bytesPerPixel;
    CGColorSpaceRef colorSpace = color ? CGColorSpaceCreateDeviceRGB() : CGColorSpaceCreateDeviceGray();
    if (colorSpace == nullptr) {
        return;
    }
    const CGBitmapInfo bitmapInfo = color ? (CGBitmapInfo)(kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big) : (CGBitmapInfo)(kCGImageAlphaNone);
    CGContextRef context = CGBitmapContextCreate(buf, (size_t)(cells)*metrics_.width, metrics_.height, 8, stride, colorSpace, bitmapInfo);
    CGColorSpaceRelease(colorSpace);
    if (context == nullptr) {
        return;
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
        matrix.c = 0.25;
    }
    CGContextSetTextMatrix(context, matrix);

    // One CTLine over the whole span - Core Text forms the ligatures -
    // but the glyphs draw at cluster-snapped positions: a font advance
    // disagreeing with the cell width must not accumulate across a long
    // run, so every cluster re-bases at its grid column and keeps its
    // intra-cluster offsets.
    columns_.reset();
    columns_.grow(2 * count * sizeof(u16));
    auto* const utf16Columns = (u16*)(columns_.mutData());
    size_t utf16Length = 0;
    {
        size_t position = 0;
        u16 column = 0;
        SpanCluster cluster;
        while (nextSpanCluster(codepoints, count, position, cluster)) {
            for (size_t index = cluster.begin; index < cluster.begin + cluster.count; ++index) {
                const size_t units = codepoints[index] > 0xffff ? 2 : 1;
                for (size_t unit = 0; unit < units; ++unit) {
                    utf16Columns[utf16Length++] = column;
                }
            }
            column = (u16)(column + cluster.cells);
        }
    }
    CFStringRef string = makeString(codepoints, count);
    if (string != nullptr) {
        CTLineRef line = makeLine(string);
        CFRelease(string);
        if (line != nullptr) {
            bool lineColor = false;
            if (inspectLine(line, lineColor) && lineColor == color) {
                const CGFloat baselineY = (CGFloat)(metrics_.height - metrics_.baseline);
                CFArrayRef runs = CTLineGetGlyphRuns(line);
                const CFIndex runCount = CFArrayGetCount(runs);
                for (CFIndex runIndex = 0; runIndex < runCount; ++runIndex) {
                    auto run = (CTRunRef)(CFArrayGetValueAtIndex(runs, runIndex));
                    const CFIndex glyphCount = CTRunGetGlyphCount(run);
                    if (glyphCount <= 0) {
                        continue;
                    }
                    runScratch_.reset();
                    runScratch_.grow((size_t)(glyphCount) * (sizeof(CGGlyph) + sizeof(CGPoint) * 2 + sizeof(CFIndex)));
                    auto* const glyphs = (CGGlyph*)(runScratch_.mutData());
                    auto* const natural = (CGPoint*)(glyphs + glyphCount);
                    auto* const adjusted = natural + glyphCount;
                    auto* const indices = (CFIndex*)(adjusted + glyphCount);
                    CTRunGetGlyphs(run, {0, glyphCount}, glyphs);
                    CTRunGetPositions(run, {0, glyphCount}, natural);
                    CTRunGetStringIndices(run, {0, glyphCount}, indices);
                    CFIndex index = 0;
                    while (index < glyphCount) {
                        const CFIndex clusterIndex = indices[index];
                        const CGFloat target = clusterIndex >= 0 && (size_t)(clusterIndex) < utf16Length ? (CGFloat)((size_t)(utf16Columns[clusterIndex]) * metrics_.width) : natural[index].x;
                        const CGFloat base = natural[index].x;
                        while (index < glyphCount && indices[index] == clusterIndex) {
                            adjusted[index].x = target + (natural[index].x - base);
                            adjusted[index].y = baselineY + natural[index].y;
                            ++index;
                        }
                    }
                    CFDictionaryRef attributes = CTRunGetAttributes(run);
                    auto runFont = (CTFontRef)(CFDictionaryGetValue(attributes, kCTFontAttributeName));
                    CTFontDrawGlyphs(runFont != nullptr ? runFont : font_, glyphs, adjusted, (size_t)(glyphCount), context);
                }
            }
            CFRelease(line);
        }
    }
    CGContextRelease(context);
}

bool CoreTextFont::colored() const {
    return (CTFontGetSymbolicTraits(font_) & kCTFontColorGlyphsTrait) != 0;
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
    // Core Text turns ligatures off by default for fixed-pitch fonts;
    // level 2 (all ligatures) is what coding fonts expect - the same
    // opt-in iTerm2 makes.
    const int ligatureLevel = 2;
    CFNumberRef ligatures = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &ligatureLevel);
    if (ligatures == nullptr) {
        return nullptr;
    }
    const void* keys[] = {
        kCTFontAttributeName,
        kCTForegroundColorFromContextAttributeName,
        kCTLigatureAttributeName,
    };
    const void* values[] = {
        font_,
        kCFBooleanTrue,
        ligatures,
    };
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(ligatures);
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

// The resolved artifact is the file behind the CTFont plus the index of
// its face inside the collection, found by PostScript name among the
// file's descriptors. Path requests fall through to the generic mmap
// resolver.
FontFace* CoreTextFontResolver::extractFace(CTFontRef font) {
    auto url = (CFURLRef)(CTFontCopyAttribute(font, kCTFontURLAttribute));
    if (url == nullptr) {
        return nullptr;
    }
    char path[4096];
    if (!CFURLGetFileSystemRepresentation(url, true, (UInt8*)(path), sizeof(path))) {
        CFRelease(url);
        return nullptr;
    }
    i32 faceIndex = 0;
    CFStringRef wanted = CTFontCopyPostScriptName(font);
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);
    if (descriptors != nullptr) {
        const CFIndex count = CFArrayGetCount(descriptors);
        for (CFIndex index = 0; index < count; ++index) {
            auto descriptor = (CTFontDescriptorRef)(CFArrayGetValueAtIndex(descriptors, index));
            auto name = (CFStringRef)(CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute));
            const bool matches = sameString(wanted, name);
            if (name != nullptr) {
                CFRelease(name);
            }
            if (matches) {
                faceIndex = (i32)(index);
                break;
            }
        }
        CFRelease(descriptors);
    }
    if (wanted != nullptr) {
        CFRelease(wanted);
    }
    return openFontFile(StringView(path), faceIndex);
}

FontFace* CoreTextFontResolver::resolve(const FontRequest& request) {
    if (pathName(request.name)) {
        return nullptr;
    }
    CTFontRef font = resolveName(request);
    if (font == nullptr) {
        return nullptr;
    }
    FontFace* face = nullptr;
    try {
        face = extractFace(font);
    } catch (Exception&) {
    }
    CFRelease(font);
    return face;
}

CTFontRef CoreTextFontRenderer::openFace(const FontFace& face, u16 pixels) {
    // CFData owns a copy of the bytes: a handful of faces per session is
    // cheap, and no CoreText cache can outlive our mapping.
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)(face.data()), (CFIndex)(face.size()));
    if (data == nullptr) {
        return nullptr;
    }
    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromData(data);
    CFRelease(data);
    if (descriptors == nullptr) {
        return nullptr;
    }
    CTFontRef font = nullptr;
    const CFIndex count = CFArrayGetCount(descriptors);
    if (count > 0) {
        const CFIndex index = face.faceIndex() >= 0 && face.faceIndex() < count ? face.faceIndex() : 0;
        auto descriptor = (CTFontDescriptorRef)(CFArrayGetValueAtIndex(descriptors, index));
        font = CTFontCreateWithFontDescriptor(descriptor, pixels, nullptr);
    }
    CFRelease(descriptors);
    return font;
}

FontMetrics CoreTextFontRenderer::measure(CTFontRef font) {
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

Font* CoreTextFontRenderer::render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) {
    CTFontRef font = openFace(*face, pixels);
    if (font == nullptr) {
        return nullptr;
    }
    const FontMetrics actual = measure(font);
    if (actual.width == 0 || actual.height == 0 || actual.baseline == 0) {
        CFRelease(font);
        return nullptr;
    }
    if (opts.verbose) {
        sysO << StringView(u8"coretext face: kind ") << (u64)((u8)(kind)) << StringView(u8" at ") << pixels << StringView(u8"px, cell ") << actual.width << StringView(u8"x") << actual.height << StringView(u8" baseline ") << actual.baseline << StringView(u8"\n");
    }
    if (kind == FontKind::Primary) {
        metrics = actual;
    } else if (kind == FontKind::Overlay && (metrics.height != actual.height || metrics.baseline != actual.baseline)) {
        CFRelease(font);
        return nullptr;
    }
    return owner.make<CoreTextFont>(face, font, kind, metrics, FontStyle::Regular);
}

FontResolver* createCoreTextFontResolver(Composer& composer) {
    return composer.pool->make<CoreTextFontResolver>();
}

FontRenderer* createCoreTextFontRenderer(Composer& composer) {
    return composer.pool->make<CoreTextFontRenderer>();
}
#else
FontResolver* createCoreTextFontResolver(Composer& composer) {
    (void)(composer);
    return nullptr;
}

FontRenderer* createCoreTextFontRenderer(Composer& composer) {
    (void)(composer);
    return nullptr;
}
#endif
