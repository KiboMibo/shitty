/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Shitty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "font.h"
#include "grapheme.h"
#include "log.h"
#include "options.h"
#include "utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>


namespace stl {}
using namespace stl;

Font::Font(const std::string& filename_)
    : filename(filename_)
    , overlay(false)
{
    load();
}

Font::Font(const std::string& filename_, const Font& priFont, Overlay_)
    : filename(filename_)
    , overlay(true)
    , px(priFont.getPx())
    , py(priFont.getPy())
    , baseline(priFont.getBaseline())
    , nx(priFont.getNx())
    , ny(priFont.getNy())
    , atlasBuf(priFont.getAtlas())
    , atlasMap(priFont.getAtlasMap())
{
    load();
}

Font::Font(const std::string& filename_, const Font& priFont, DoubleWidth_)
    : filename(filename_)
    , dwidth(true)
    , px(2 * priFont.getPx())
    , py(priFont.getPy())
{
    load();
}

bool Font::isLoadableChar(FT_ULong c) {
    if (c == Missing_Glyph_Marker) {
        return true;
    }

    if (c == Unicode_Replacement_Character) {
        return true;
    }

    return ((dwidth && codepointWidth(c) == 2) || (!dwidth && codepointWidth(c) < 2));
}

void Font::load() {
    FT_Library ft = nullptr;
    FT_Face face = nullptr;

    struct Cleanup {
        FT_Library& library;
        FT_Face& face;

        ~Cleanup() {
            if (face) {
                FT_Done_Face(face);
            }
            if (library) {
                FT_Done_FreeType(library);
            }
        }
    } cleanup{ft, face};

    if (FT_Init_FreeType(&ft)) {
        throw std::runtime_error("Could not initialize FreeType library");
    }
    logI << "Loading " << filename << " as " << (overlay ? "overlay" : (dwidth ? "double-width" : "primary")) << std::endl;
    if (FT_New_Face(ft, filename.c_str(), 0, &face)) {
        throw std::runtime_error(std::string("Failed to load font ") + filename);
    }

    /* Determine the number of glyphs to actually load, based on wcwidth ()
       * We need this number up front to compute the atlas geometry.
       */
    int num_glyphs = 0;
    {
        FT_UInt gindex;
        FT_ULong charcode = FT_Get_First_Char(face, &gindex);
        while (gindex != 0) {
            if (isLoadableChar(charcode)) {
                ++num_glyphs;
            }
            charcode = FT_Get_Next_Char(face, charcode, &gindex);
        }
    }

    logT << "Family: " << face->family_name << "; Style: " << face->style_name << "; Faces: " << face->num_faces << "; Glyphs: " << num_glyphs << " to load (" << face->num_glyphs << " total)" << std::endl;

    if (face->num_fixed_sizes > 0) {
        loadFixed(face);
    } else {
        loadScaled(face);
    }

    /* Given that we have num_glyphs glyphs to load, with each
       * individual glyph having a size of px * py, compute nx and ny so
       * that the resulting atlas texture geometry is closest to a square.
       * We use one extra glyph space to guarantee a blank glyph at (0,0).
       */
    if (!overlay) {
        unsigned n_glyphs = num_glyphs + 1;
        unsigned long total_pixels = n_glyphs * px * py;
        double side = sqrt(total_pixels);
        nx = side / px;
        ny = side / py;
        while ((unsigned)nx * ny < n_glyphs) {
            if (px * nx < py * ny) {
                ++nx;
            } else {
                ++ny;
            }
        }

        if (nx > 255 || ny > 255) {
            logE << "Atlas geometry not addressable by single byte coords. "
                 << "Please report this as a bug with your font attached!" << std::endl;
            throw std::runtime_error("Impossible atlas geometry");
        }

        logT << "Atlas texture geometry: " << nx << "x" << ny << " glyphs of " << px << "x" << py << " each, "
             << "yielding pixel size " << nx * px << "x" << ny * py << "." << std::endl;
        logT << "Atlas holds space for " << nx * ny << " glyphs, " << n_glyphs << " will be used, empty: " << nx * ny - n_glyphs << " (" << 100.0 * (nx * ny - n_glyphs) / (nx * ny) << "%)" << std::endl;

        size_t atlas_bytes = nx * px * ny * py;
        logT << "Allocating " << atlas_bytes << " bytes for atlas buffer" << std::endl;
        atlasBuf.resize(atlas_bytes, 0);
    }

    FT_UInt gindex;
    FT_ULong charcode = FT_Get_First_Char(face, &gindex);
    while (gindex != 0) {
        if (isLoadableChar(charcode)) {
            if (overlay) {
                const auto& it = atlasMap.find(charcode);
                if (it != atlasMap.end()) {
                    loadFace(face, charcode, it->second);
                }
            } else {
                loadFace(face, charcode);
            }
        }
        charcode = FT_Get_Next_Char(face, charcode, &gindex);
    }
}

void Font::loadFixed(const FT_Face& face) {
    int bestIdx = -1;
    int bestHeightDiff = std::numeric_limits<int>::max();
    {
        std::ostringstream oss;
        oss << "Available sizes:";
        for (int i = 0; i < face->num_fixed_sizes; ++i) {
            oss << " " << face->available_sizes[i].width << "x" << face->available_sizes[i].height;

            int diff = abs(opts.fontsize - face->available_sizes[i].height);
            if (diff < bestHeightDiff) {
                bestIdx = i;
                bestHeightDiff = diff;
            }
        }
        logT << oss.str() << std::endl;
    }

    logT << "Configured size: " << (int)opts.fontsize << "; Best matching fixed size: " << face->available_sizes[bestIdx].width << "x" << face->available_sizes[bestIdx].height << std::endl;

    if (bestHeightDiff > 1 && face->units_per_EM > 0) {
        logT << "Size mismatch too large, fallback to rendering outlines." << std::endl;
        loadScaled(face);
        return;
    }

    const auto& facesize = face->available_sizes[bestIdx];

    if (overlay || dwidth) {
        if (px != facesize.width) {
            throw std::runtime_error(filename + ": size mismatch, expected px=" + std::to_string(px) + ", got: " + std::to_string(facesize.width));
        }
        if (py != facesize.height) {
            throw std::runtime_error(filename + ": size mismatch, expected py=" + std::to_string(py) + ", got: " + std::to_string(facesize.height));
        }
    } else {
        px = facesize.width;
        py = facesize.height;
        baseline = 0;
    }
    logI << "Glyph size " << px << "x" << py << std::endl;

    if (FT_Set_Pixel_Sizes(face, px, py)) {
        throw std::runtime_error("Could not set pixel sizes");
    }

    if (!overlay && face->height) {
        baseline = round(py * (double)face->ascender / face->height);
    }
}

void Font::loadScaled(const FT_Face& face) {
    logI << "Pixel size " << (int)opts.fontsize << std::endl;
    if (FT_Set_Pixel_Sizes(face, opts.fontsize, opts.fontsize)) {
        throw std::runtime_error("Could not set pixel sizes");
    }

    double tpx = opts.fontsize * (double)face->max_advance_width / face->units_per_EM;
    double tpy = tpx * face->height / face->max_advance_width + 1;
    const u16 facePx = round(tpx);
    const u16 facePy = round(tpy);
    const u16 faceBaseline = round(tpy * face->ascender / face->height);
    if ((overlay || dwidth) && (px != facePx || py != facePy)) {
        throw std::runtime_error(filename + ": scaled metric mismatch, expected " + std::to_string(px) + "x" + std::to_string(py) + ", got " + std::to_string(facePx) + "x" + std::to_string(facePy));
    }
    if (overlay && baseline != faceBaseline) {
        throw std::runtime_error(filename + ": scaled baseline mismatch, expected " + std::to_string(baseline) + ", got " + std::to_string(faceBaseline));
    }
    if (!overlay && !dwidth) {
        px = facePx;
        py = facePy;
    }
    if (!overlay) {
        baseline = faceBaseline;
    }
    logI << "Glyph size " << px << "x" << py << ", baseline " << baseline << std::endl;
}

void Font::loadFace(const FT_Face& face, FT_ULong c) {
    const u8 atlas_row = atlas_seq / nx;
    const u8 atlas_col = atlas_seq - nx * atlas_row;
    const AtlasPos apos = {atlas_col, atlas_row};

    loadFace(face, c, apos);
    atlasMap[c] = apos;
    ++atlas_seq;
}

void Font::loadFace(const FT_Face& face, FT_ULong c, const AtlasPos& apos) {
    if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
        throw std::runtime_error(std::string("FreeType: Failed to load glyph for char ") + std::to_string(c));
    }

    int dx = face->glyph->bitmap_left;
    int dy = baseline > 0 ? baseline - face->glyph->bitmap_top : 0;

    const int sh = std::max(0, -dy);
    const int sw = std::max(0, -dx);
    dx += sw;
    dy += sh;

    const auto& bmp = face->glyph->bitmap;
    const int bh = std::min({(int)bmp.rows, (int)py, py - dy + sh});
    const int bw = std::min({(int)bmp.width, (int)px, px - dx + sw});

    const int atlas_row_offset = nx * px * py;
    const int atlas_glyph_offset = apos.y * atlas_row_offset + apos.x * px;
    const int atlas_write_offset = atlas_glyph_offset + nx * px * dy + dx;

    if (overlay) {
        for (int j = 0; j < bh; ++j) {
            u8* atl_dst_row = atlasBuf.data() + atlas_glyph_offset + j * nx * px;
            for (int k = 0; k < bw; ++k) {
                *atl_dst_row++ = 0;
            }
        }
    }

    /* Load bitmap into atlas buffer area. Each row in the bitmap
       * occupies bitmap.pitch bytes (with padding); this is the
       * increment in the input bitmap array per row.
       *
       * Interpretation of bytes within the bitmap rows is subject to
       * bitmap.pixel_mode, essentially either 8 bits (256-scale gray)
       * per pixel, or 1 bit (mono) per pixel. Leftmost pixel is MSB.
       *
       */
    const unsigned char* bmp_src_row;
    u8* atl_dst_row;
    switch (bmp.pixel_mode) {
        case FT_PIXEL_MODE_MONO:
            for (int j = sh; j < bh; ++j) {
                bmp_src_row = bmp.buffer + j * bmp.pitch;
                atl_dst_row = atlasBuf.data() + atlas_write_offset + j * nx * px;
                u8 byte = 0;
                for (int k = 0; k < bw; ++k) {
                    if (k % 8 == 0) {
                        byte = *bmp_src_row++;
                    }
                    if (k >= sw) {
                        *atl_dst_row++ = (byte & 0x80) ? 0xFF : 0;
                    }
                    byte <<= 1;
                }
            }
            break;
        case FT_PIXEL_MODE_GRAY:
            for (int j = sh; j < bh; ++j) {
                bmp_src_row = bmp.buffer + j * bmp.pitch + sw;
                atl_dst_row = atlasBuf.data() + atlas_write_offset + j * nx * px;
                for (int k = sw; k < bw; ++k) {
                    *atl_dst_row++ = *bmp_src_row++;
                }
            }
            break;
        default:
            throw std::runtime_error(std::string("Unhandled pixel_type=") + std::to_string(bmp.pixel_mode));
    }
}
