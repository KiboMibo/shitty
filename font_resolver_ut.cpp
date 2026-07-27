/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_resolver.h"

#include "composer.h"
#include "font_pack.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct FakeFont final: public Font {
        FontGlyph glyph(const u32* codepoints, size_t count) override;
    };

    struct RecordingResolver final: public FontResolver {
        explicit RecordingResolver(bool accepts);

        Font* load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) override;

        FontRequest requests[8]{};
        size_t calls = 0;
        bool accepts_;
    };

    void removeDefaultResolvers(Composer& composer) {
        while (!composer.fontResolvers.empty()) {
            composer.fontResolvers.popFront();
        }
    }
}

FontGlyph FakeFont::glyph(const u32*, size_t) {
    return {};
}

RecordingResolver::RecordingResolver(bool accepts)
    : accepts_(accepts)
{
}

Font* RecordingResolver::load(ObjPool& owner, const FontRequest& request, FontMetrics& metrics) {
    requests[calls++] = request;
    if (!accepts_) {
        return nullptr;
    }
    if (request.kind == FontKind::Primary) {
        metrics = {8, 16, 12};
    }
    return owner.make<FakeFont>();
}

STD_TEST_SUITE(FontResolver) {
    STD_TEST(UsesFirstSuccessfulResolver) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        removeDefaultResolvers(composer);
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
        Composer composer(pool.mutPtr());
        removeDefaultResolvers(composer);
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
        Composer composer(composerPool.mutPtr());
        removeDefaultResolvers(composer);
        RecordingResolver resolver(true);
        composer.fontResolvers.pushBack(&resolver);
        ObjPool::Ref fontPool = ObjPool::fromMemory();

        Fontpack* const fonts = Fontpack::create(composer, *fontPool, StringView(u8"primary"), StringView(u8"wide"), 21);

        STD_INSIST(fonts->getPx() == 8);
        STD_INSIST(fonts->getPy() == 16);
        STD_INSIST(fonts->hasBold());
        STD_INSIST(fonts->hasItalic());
        STD_INSIST(fonts->hasBoldItalic());
        STD_INSIST(fonts->hasDoubleWidth());
        STD_INSIST(resolver.calls == 5);
        STD_INSIST(resolver.requests[0].name == StringView(u8"primary"));
        STD_INSIST(resolver.requests[0].style == FontStyle::Regular);
        STD_INSIST(resolver.requests[0].kind == FontKind::Primary);
        STD_INSIST(resolver.requests[1].style == FontStyle::Bold);
        STD_INSIST(resolver.requests[1].kind == FontKind::Overlay);
        STD_INSIST(resolver.requests[2].style == FontStyle::Italic);
        STD_INSIST(resolver.requests[3].style == FontStyle::BoldItalic);
        STD_INSIST(resolver.requests[4].name == StringView(u8"wide"));
        STD_INSIST(resolver.requests[4].style == FontStyle::Regular);
        STD_INSIST(resolver.requests[4].kind == FontKind::DoubleWidth);
    }
}
