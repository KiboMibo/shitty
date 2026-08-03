/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_resolver.h"

#include "composer.h"
#include "font_face.h"
#include "font_pack.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct FakeFont final: public Font {
        FontGlyph glyph(const u32* codepoints, size_t count, u16 cells) override;
        void render(const u32* codepoints, size_t count, u16 cells, void* buf) override;
        bool covers(u32 codepoint) override;
        bool colored() const override;
        Font* synthesize(ObjPool& owner, FontStyle style) override;
        FontFace* face() override;
    };

    struct RecordingResolver final: public FontResolver {
        explicit RecordingResolver(bool accepts);

        FontFace* resolve(const FontRequest& request) override;

        FontRequest requests[8]{};
        size_t calls = 0;
        bool accepts_;
    };

    struct FakeRenderer final: public FontRenderer {
        Font* render(ObjPool& owner, IntrusivePtr<FontFace> face, u16 pixels, FontKind kind, FontMetrics& metrics) override;

        size_t rendered = 0;
    };

    const u8 fakeFontBytes[] = {0};

    void removeDefaultResolvers(Composer& composer) {
        while (!composer.fontResolvers.empty()) {
            composer.fontResolvers.popFront();
        }
        while (!composer.fontRenderers.empty()) {
            composer.fontRenderers.popFront();
        }
    }
}

FontGlyph FakeFont::glyph(const u32*, size_t, u16) {
    return {};
}

void FakeFont::render(const u32*, size_t, u16, void*) {
}

bool FakeFont::colored() const {
    return false;
}

bool FakeFont::covers(u32) {
    return false;
}

Font* FakeFont::synthesize(ObjPool&, FontStyle) {
    return nullptr;
}

FontFace* FakeFont::face() {
    return nullptr;
}

RecordingResolver::RecordingResolver(bool accepts)
    : accepts_(accepts)
{
}

FontFace* RecordingResolver::resolve(const FontRequest& request) {
    requests[calls++] = request;
    if (!accepts_) {
        return nullptr;
    }
    return createMemoryFontFace(fakeFontBytes, sizeof(fakeFontBytes), 0);
}

Font* FakeRenderer::render(ObjPool& owner, IntrusivePtr<FontFace>, u16, FontKind kind, FontMetrics& metrics) {
    ++rendered;
    if (kind == FontKind::Primary) {
        metrics = {8, 16, 12};
    }
    return owner.make<FakeFont>();
}

STD_TEST_SUITE(FontResolver) {
    STD_TEST(UsesFirstSuccessfulResolver) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        removeDefaultResolvers(composer);
        FakeRenderer renderer;
        composer.fontRenderers.pushBack(&renderer);
        RecordingResolver first(false);
        RecordingResolver second(true);
        RecordingResolver third(true);
        composer.fontResolvers.pushBack(&first);
        composer.fontResolvers.pushBack(&second);
        composer.fontResolvers.pushBack(&third);
        FontMetrics metrics;
        const FontRequest request{StringView(u8"example"), 19, FontStyle::Italic, FontKind::Overlay};

        Font* const font = composer.loadFont(*pool, request, metrics);

        STD_INSIST(font != nullptr);
        STD_INSIST(first.calls == 1);
        STD_INSIST(second.calls == 1);
        STD_INSIST(third.calls == 0);
        STD_INSIST(second.requests[0].name == StringView(u8"example"));
        STD_INSIST(second.requests[0].pixels == 19);
        STD_INSIST(second.requests[0].style == FontStyle::Italic);
        STD_INSIST(second.requests[0].kind == FontKind::Overlay);
    }

    STD_TEST(ReturnsNullAfterEveryResolverDeclines) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        removeDefaultResolvers(composer);
        FakeRenderer renderer;
        composer.fontRenderers.pushBack(&renderer);
        RecordingResolver first(false);
        RecordingResolver second(false);
        composer.fontResolvers.pushBack(&first);
        composer.fontResolvers.pushBack(&second);
        FontMetrics metrics;

        Font* const font = composer.loadFont(*pool, {StringView(u8"missing"), 16, FontStyle::Regular, FontKind::Primary}, metrics);

        STD_INSIST(font == nullptr);
        STD_INSIST(first.calls == 1);
        STD_INSIST(second.calls == 1);
    }

    STD_TEST(FontpackLoadsEveryRoleThroughComposer) {
        ObjPool::Ref composerPool = ObjPool::fromMemory();
        Composer& composer = *composerPool->make<Composer>(composerPool.mutPtr());
        removeDefaultResolvers(composer);
        FakeRenderer renderer;
        composer.fontRenderers.pushBack(&renderer);
        RecordingResolver resolver(true);
        composer.fontResolvers.pushBack(&resolver);
        ObjPool::Ref fontPool = ObjPool::fromMemory();

        const StringView names[] = {StringView(u8"primary"), StringView(u8"extra")};
        Fontpack* const fonts = Fontpack::create(composer, *fontPool, names, 2, 21);

        STD_INSIST(fonts->getPx() == 8);
        STD_INSIST(fonts->getPy() == 16);
        STD_INSIST(fonts->hasBold());
        STD_INSIST(fonts->hasItalic());
        STD_INSIST(fonts->hasBoldItalic());
        STD_INSIST(resolver.calls == 5);
        STD_INSIST(resolver.requests[0].name == StringView(u8"primary"));
        STD_INSIST(resolver.requests[0].style == FontStyle::Regular);
        STD_INSIST(resolver.requests[0].kind == FontKind::Primary);
        STD_INSIST(resolver.requests[1].style == FontStyle::Bold);
        STD_INSIST(resolver.requests[1].kind == FontKind::Overlay);
        STD_INSIST(resolver.requests[2].style == FontStyle::Italic);
        STD_INSIST(resolver.requests[3].style == FontStyle::BoldItalic);
        STD_INSIST(resolver.requests[4].name == StringView(u8"extra"));
        STD_INSIST(resolver.requests[4].style == FontStyle::Regular);
        STD_INSIST(resolver.requests[4].kind == FontKind::Fallback);
    }
}
