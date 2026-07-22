/* This file is part of Shitty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

class Font {
public:
    enum Overlay_ {
        Overlay
    };

    enum DoubleWidth_ {
        DoubleWidth
    };

    /* Load a primary font, determining the atlas geometry and setting up
       * a mapping from unicode code points to atlas grid positions.
       */
    explicit Font(const std::string& filename);

    /* Load an alternate font based on an already loaded primary font,
       * conforming to the same atlas geometry (incl. position mapping)
       * and starting with a copy of the atlas texture data.
       *
       * It is an error if the alternate font has different geometry.
       * Any code point not having a glyph in the alternate font will
       * have the glyph of the primary font (if any) in its atlas.
       * Any code point not having a glyph in the primary font will be
       * discarded.
       */
    Font(const std::string& filename, const Font& priFont, Overlay_);

    /* Load a double-width font based on an already loaded primary font.
       * A double-width font is less tightly coupled to the primary,
       * but its glyph size has to match (double width, equal height).
       * The font will have its own independent atlas geometry.
       *
       * Only code points that are considered double-width by wcwidth ()
       * will be loaded.
       */
    Font(const std::string& filename, const Font& priFont, DoubleWidth_);

    ~Font() = default;

    u16 getPx() const {
        return px;
    };

    u16 getPy() const {
        return py;
    };

    u16 getBaseline() const {
        return baseline;
    };

    u16 getNx() const {
        return nx;
    };

    u16 getNy() const {
        return ny;
    };

    const std::vector<u8>& getAtlas() const {
        return atlasBuf;
    };

    const u8* getAtlasData() const {
        return atlasBuf.data();
    };

    struct AtlasPos {
        u8 x = 0;
        u8 y = 0;
    };

    using AtlasMap = std::map<u32, AtlasPos>;

    const AtlasMap& getAtlasMap() const {
        return atlasMap;
    };

private:
    std::string filename;
    bool overlay = false;
    bool dwidth = false;
    u16 px = 0;
    u16 py = 0;
    u16 baseline = 0;
    u16 nx = 0;
    u16 ny = 0;
    std::vector<u8> atlasBuf;
    AtlasMap atlasMap;

    /* Start with 1 so as to leave a blank glyph at (0,0).
       * That blank will get referenced for any out-of-bounds text position
       * lookup in the shader, and guarantees that no fractional glyphs will
       * be shown at the right and bottom edges.
       * Also, any glyph mapping lookup that results in (0,0) means that the
       * character code does not exist in the atlas.
       */
    u16 atlas_seq = 1;

    /* Load font from glyph bitmaps rasterized by FreeType.
       * Store the bitmaps into an atlas bitmap stored in atlasBuf.
       */
    bool isLoadableChar(FT_ULong c);
    void load();
    void loadFixed(const FT_Face& face);
    void loadScaled(const FT_Face& face);
    void loadFace(const FT_Face& face, FT_ULong c);
    void loadFace(const FT_Face& face, FT_ULong c, const AtlasPos& apos);
};
