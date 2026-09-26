// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

// Cursor handle -> premultiplied ARGB image + hotspot, and the sharp-bilinear
// upscaling used by the AnyDesk cursor overlay (Specification.md section 10.4).
// Everything but Read() is pure pixel work, so the unit tests drive it directly.
namespace cursorimg {

// Top-down, premultiplied 0xAARRGGBB pixels (the layout UpdateLayeredWindow wants).
struct Image {
    int w = 0, h = 0;
    POINT hot{};
    std::vector<uint32_t> px;

    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }
};

// Decodes the bitmaps GetIconInfo hands out, already read as top-down 32-bit
// pixels. `color` is null for a monochrome cursor; `mask` then has double height
// (AND above XOR). A mask pixel counts as set when its RGB part is non-zero.
// `inverting` receives one flag per output pixel for the pixels that invert the
// screen (they cannot be reproduced by an overlay); they are left transparent.
void Decode(int w, int h, const uint32_t* color, const uint32_t* mask, POINT hot,
            Image& out, std::vector<uint8_t>& inverting);

// Draws inverting pixels opaque black and every transparent pixel within
// `radius` of one opaque white, so an inverting I-beam stays visible on any
// background. The canvas grows by `radius` on each side (hotspot included) when
// there is anything to outline, so the outline is never cut off at the edge.
void AddOutline(Image& img, const std::vector<uint8_t>& inverting, int radius);

// Outline radius for the monitor the cursor is on: max(1, round(dpi/96 * 0.8)).
int OutlineRadius(UINT dpi);

// Sharp bilinear: nearest neighbour by the integer part k = max(1, zoom/100),
// then bilinear to the exact size round(size * zoom / 100), edges clamped.
// The hotspot is scaled the same way.
Image Scale(const Image& src, int zoomPercent);

// Height of the visible part (rows with any non-transparent pixel), 0 if none.
int VisibleHeight(const Image& img);

// A fingerprint of the picture (size, hotspot, pixels). Cursor handles cannot
// tell shapes apart: AnyDesk destroys cursors and gets the same handle back.
uint64_t Key(const Image& img);

// Reads a live cursor (any process's: cursor handles are valid system-wide),
// including the outline for inverting pixels. False if it has no usable image.
bool Read(HCURSOR cursor, UINT dpi, Image& out);

} // namespace cursorimg
