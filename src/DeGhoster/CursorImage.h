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
// upscaling used by the remote cursor overlay (Specification.md section 10.4).
// Everything but Read() is pure pixel work, so the unit tests drive it directly.
namespace cursorimg {

// Top-down, premultiplied 0xAARRGGBB pixels (the layout UpdateLayeredWindow wants).
// `xr` holds the pixels that change the screen instead of covering it: empty
// when there are none, otherwise one entry per pixel, 0 for an ordinary pixel and
// 0xFF000000 | rgb for one that shows the screen XOR rgb there (rgb = white:
// inverted). Those pixels are transparent in `px`.
struct Image {
    int w = 0, h = 0;
    POINT hot{};
    std::vector<uint32_t> px;
    std::vector<uint32_t> xr;

    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    bool inverts() const { return !xr.empty(); }
};

// Decodes the bitmaps GetIconInfo hands out, already read as top-down 32-bit
// pixels. `color` is null for a monochrome cursor; `mask` then has double height
// (AND above XOR). A mask pixel counts as set when its RGB part is non-zero.
// Pixels that XOR the screen (AND 1 with a non-black colour or XOR bit) go to
// `out.xr`.
void Decode(int w, int h, const uint32_t* color, const uint32_t* mask, POINT hot, Image& out);

// The stand-in for an overlay that cannot read the screen under itself: draws
// the XOR pixels opaque black and every transparent pixel within `radius` of one
// opaque white, so an inverting I-beam stays visible on any background, and
// drops `xr`. The canvas grows by `radius` on each side (hotspot included) when
// there is anything to outline, so the outline is never cut off at the edge.
void AddOutline(Image& img, int radius);

// The inverting pixels over a background, for a colour-keyed window: `screen`
// holds the w x h screen pixels under the image (0x00RRGGBB, top-down); `out`
// (may be `screen`) gets the screen pixel XOR its colour at every XOR pixel and
// `key` everywhere else. A result that happens to equal `key` is nudged by one
// step, so it does not turn transparent.
void XorLayer(const Image& img, const uint32_t* screen, uint32_t* out, uint32_t key);

// Outline radius for the monitor the cursor is on: max(1, round(dpi/96 * 0.8)).
int OutlineRadius(UINT dpi);

// Sharp bilinear: nearest neighbour by the integer part k = max(1, zoom/100),
// then bilinear to the exact size round(size * zoom / 100), edges clamped.
// The hotspot is scaled the same way. XOR pixels stay crisp: an output pixel is
// one where the bilinear coverage of XOR pixels reaches one half, with the
// colour of the nearest of them.
Image Scale(const Image& src, int zoomPercent);

// Height of the visible part: the rows with a pixel of at least 50 % opacity or
// an XOR pixel, 0 if none. The threshold leaves out soft drop shadows (a Mac
// arrow's shadow adds a third to its height), which would otherwise make that
// cursor look too tall.
int VisibleHeight(const Image& img);
constexpr uint32_t kVisibleAlpha = 128;

// A fingerprint of the picture (size, hotspot, pixels, XOR pixels). Cursor
// handles cannot tell shapes apart: AnyDesk destroys cursors and gets the same
// handle back.
uint64_t Key(const Image& img);

// Reads a live cursor (any process's: cursor handles are valid system-wide).
// False if it has no usable image.
bool Read(HCURSOR cursor, Image& out);

} // namespace cursorimg
