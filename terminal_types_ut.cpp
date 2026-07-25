/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "terminal_types.h"

#include "hex.h"
#include "point.h"
#include "rect.h"
#include "vk_renderer.h"

#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(CellColor) {
    STD_TEST(PreservesEveryColorSource) {
        const CellColor foreground = CellColor::defaultForeground();
        const CellColor background = CellColor::defaultBackground();
        const CellColor indexed = CellColor::indexed(217);
        const CellColor direct = CellColor::direct({1, 2, 3});

        STD_INSIST(foreground.source() == CellColor::Source::DefaultForeground);
        STD_INSIST(background.source() == CellColor::Source::DefaultBackground);
        STD_INSIST(indexed.source() == CellColor::Source::Indexed);
        STD_INSIST(indexed.index() == 217);
        STD_INSIST(direct.source() == CellColor::Source::Direct);
        STD_INSIST((direct.color() == Color{1, 2, 3}));
        STD_INSIST(CellColor::fromEncoded(direct.encoded()) == direct);
    }

    STD_TEST(ReportsLegacyIndices) {
        STD_INSIST(CellColor::indexed(7).legacyIndex() == 7);
        STD_INSIST(CellColor::direct({1, 2, 3}).legacyIndex() == -1);
        STD_INSIST(CellColor::defaultForeground().legacyIndex() == -2);
        STD_INSIST(CellColor::defaultBackground().legacyIndex() == -2);
    }
}

STD_TEST_SUITE(TerminalCell) {
    STD_TEST(RoundTripsPackedColors) {
        const CellColor colors[] = {
            CellColor::defaultForeground(),
            CellColor::defaultBackground(),
            CellColor::indexed(255),
            CellColor::direct({17, 34, 51}),
        };

        for (CellColor color : colors) {
            TerminalCell cell{};
            cell.setForeground(color);
            cell.setBackground(color);
            STD_INSIST(cell.foreground() == color);
            STD_INSIST(cell.background() == color);
        }
    }

    STD_TEST(ExtraRefAndInlinePayloadAreExclusive) {
        TerminalCell cell{};
        const CellColor underline = CellColor::direct({9, 8, 7});
        cell.setInlineUnderlineColor(underline);

        STD_INSIST(!cell.hasExtra());
        STD_INSIST(cell.extraRef() == 0);
        STD_INSIST(cell.inlineUnderlineColor() == underline);

        cell.setExtraRef(42);

        STD_INSIST(cell.hasExtra());
        STD_INSIST(cell.extraRef() == 42);
        STD_INSIST(cell.inlineUnderlineColor() == CellColor::defaultForeground());

        cell.setInlineUnderlineColor(underline);
        STD_INSIST(!cell.hasExtra());
        STD_INSIST(cell.inlineUnderlineColor() == underline);
    }

    STD_TEST(ZeroExtraRefClearsExtendedTag) {
        TerminalCell cell{};
        cell.setExtraRef(7);
        cell.setExtraRef(0);

        STD_INSIST(!cell.hasExtra());
        STD_INSIST(cell.extraRef() == 0);
    }
}

STD_TEST_SUITE(TerminalColors) {
    STD_TEST(ResolvesDefaultIndexedAndDirectColors) {
        TerminalColors colors;
        colors.defaultForeground = {1, 2, 3};
        colors.defaultBackground = {4, 5, 6};
        colors.palette[7] = {7, 8, 9};

        STD_INSIST((colors.resolve(CellColor::defaultForeground()) == Color{1, 2, 3}));
        STD_INSIST((colors.resolve(CellColor::defaultBackground()) == Color{4, 5, 6}));
        STD_INSIST((colors.resolve(CellColor::indexed(7)) == Color{7, 8, 9}));
        STD_INSIST((colors.resolve(CellColor::direct({10, 11, 12})) == Color{10, 11, 12}));
    }

    STD_TEST(AppliesForegroundSpecialColorPriority) {
        TerminalColors colors;
        colors.defaultForeground = {1, 1, 1};
        colors.special[0] = {10, 0, 0};
        colors.special[1] = {20, 0, 0};
        colors.special[2] = {30, 0, 0};
        colors.special[4] = {40, 0, 0};
        colors.specialModes = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4);
        TerminalCell cell{};
        cell.setForeground(CellColor::defaultForeground());
        cell.bold = true;
        cell.italic = true;
        cell.underline_style = 1;
        cell.blink = true;

        STD_INSIST(colors.resolveForeground(cell) == colors.special[2]);

        cell.blink = false;
        STD_INSIST(colors.resolveForeground(cell) == colors.special[0]);
        cell.bold = false;
        STD_INSIST(colors.resolveForeground(cell) == colors.special[1]);
        cell.underline_style = 0;
        STD_INSIST(colors.resolveForeground(cell) == colors.special[4]);
    }

    STD_TEST(DoesNotOverrideExplicitForegroundWithoutOverrideMode) {
        TerminalColors colors;
        colors.palette[7] = {7, 7, 7};
        colors.special[0] = {10, 10, 10};
        colors.specialModes = 1u << 0;
        TerminalCell cell{};
        cell.setForeground(CellColor::indexed(7));
        cell.bold = true;

        STD_INSIST(colors.resolveForeground(cell) == colors.palette[7]);

        colors.specialModes |= 1u << 5;
        STD_INSIST(colors.resolveForeground(cell) == colors.special[0]);
    }

    STD_TEST(AppliesInverseBackgroundSpecialColor) {
        TerminalColors colors;
        colors.defaultBackground = {1, 2, 3};
        colors.special[3] = {4, 5, 6};
        colors.specialModes = 1u << 3;
        TerminalCell cell{};
        cell.setBackground(CellColor::defaultBackground());

        STD_INSIST(colors.resolveBackground(cell) == colors.defaultBackground);
        cell.inverse = true;
        STD_INSIST(colors.resolveBackground(cell) == colors.special[3]);
    }
}

STD_TEST_SUITE(RenderCell) {
    STD_TEST(ConstructorUsesCurrentDefaultColors) {
        RenderCell cell;

        STD_INSIST(cell.uc_pt == ' ');
        STD_INSIST(cell.fg == opts.fg);
        STD_INSIST(cell.bg == opts.bg);
        STD_INSIST(cell.underline_color == opts.fg);
        STD_INSIST(!cell.dirty);
    }

    STD_TEST(PacksShaderAttributeBits) {
        RenderCell cell;
        cell.bold = true;
        cell.italic = true;
        cell.underline = true;
        cell.inverse = true;
        cell.wrap = true;
        cell.faint = true;
        cell.blink = true;
        cell.conceal = true;
        cell.strike = true;
        cell.overline = true;
        cell.underline_style = 5;
        cell.dwidth = true;
        cell.dwidth_cont = true;
        cell.dirty = true;

        const u32 expected = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (5u << 13) | (1u << 16) | (1u << 17) | (1u << 23);
        STD_INSIST(Renderer::rendererCellAttributesForTest(cell) == expected);
    }
}

STD_TEST_SUITE(Geometry) {
    STD_TEST(OrdersPointsByRowThenColumn) {
        STD_INSIST(Point(5, 1) < Point(0, 2));
        STD_INSIST(Point(1, 1) < Point(2, 1));
        STD_INSIST(Point(2, 1) <= Point(2, 1));
        STD_INSIST(!(Point(2, 1) < Point(1, 1)));
    }

    STD_TEST(ConstructsAndClearsRectangles) {
        Rect cell(3, 4);
        STD_INSIST(cell.tl == Point(3, 4));
        STD_INSIST(cell.br == Point(4, 4));
        STD_INSIST(!cell.empty());
        STD_INSIST(cell.mid() == Point(3, 4));

        cell.toggleRectangular();
        STD_INSIST(cell.rectangular);
        cell.clear();
        STD_INSIST(cell.null());
        STD_INSIST(cell.rectangular);
    }

    STD_TEST(FormatsProjectValueTypes) {
        StringBuilder output;
        output << Point(2, 3) << StringView(u8" ") << Rect(1, 2, 3, 4) << StringView(u8" ") << Color{0x12, 0x34, 0xab};

        STD_INSIST(StringView(output) == StringView(u8"(2,3) Rect{tl=(1,2) br=(3,4) regular} rgb:1212/3434/abab"));
    }
}
