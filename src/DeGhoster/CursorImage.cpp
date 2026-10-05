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

#include "CursorImage.h"
#include <algorithm>
#include <cmath>

namespace cursorimg {

namespace {

constexpr uint32_t kBlack = 0xFF000000u;
constexpr uint32_t kWhite = 0xFFFFFFFFu;

uint32_t Premultiply(uint32_t c)
{
    const uint32_t a = c >> 24;
    if (a == 255) return c;
    auto ch = [a](uint32_t v) { return (v * a + 127) / 255; };
    return (a << 24) | (ch((c >> 16) & 0xFF) << 16) | (ch((c >> 8) & 0xFF) << 8) | ch(c & 0xFF);
}

bool Bit(const uint32_t* mask, size_t i) { return (mask[i] & 0x00FFFFFFu) != 0; }

// Reads a w x h bitmap as top-down 32-bit pixels (a mask bitmap comes back as
// 0x000000 / 0xFFFFFF per pixel).
bool ReadBits(HDC dc, HBITMAP bmp, int w, int h, std::vector<uint32_t>& px)
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    px.assign((size_t)w * h, 0);
    return GetDIBits(dc, bmp, 0, (UINT)h, px.data(), &bi, DIB_RGB_COLORS) == h;
}

} // namespace

void Decode(int w, int h, const uint32_t* color, const uint32_t* mask, POINT hot, Image& out)
{
    out.w = w; out.h = h; out.hot = hot;
    const size_t n = (size_t)w * h;
    out.px.assign(n, 0);
    out.xr.assign(n, 0);
    bool inverts = false;

    if (color) {
        bool hasAlpha = false;
        for (size_t i = 0; i < n && !hasAlpha; ++i) hasAlpha = (color[i] >> 24) != 0;
        for (size_t i = 0; i < n; ++i) {
            if (hasAlpha) {
                out.px[i] = Premultiply(color[i]);
            } else if (!Bit(mask, i)) {
                out.px[i] = 0xFF000000u | (color[i] & 0x00FFFFFFu);
            } else if ((color[i] & 0x00FFFFFFu) != 0) {
                out.xr[i] = 0xFF000000u | (color[i] & 0x00FFFFFFu);   // screen XOR colour
                inverts = true;
            }
        }
    } else {
        // Monochrome: AND rows first, XOR rows below them.
        for (size_t i = 0; i < n; ++i) {
            const bool andBit = Bit(mask, i), xorBit = Bit(mask, i + n);
            if (!andBit) {
                out.px[i] = xorBit ? kWhite : kBlack;
            } else if (xorBit) {
                out.xr[i] = kWhite;   // screen XOR white: inverted
                inverts = true;
            }
        }
    }
    if (!inverts) out.xr.clear();
}

void AddOutline(Image& img, int radius)
{
    if (!img.inverts()) return;
    if (radius < 1) radius = 1;

    const int w = img.w + 2 * radius, h = img.h + 2 * radius;
    std::vector<uint32_t> px((size_t)w * h, 0);
    std::vector<uint8_t> inv((size_t)w * h, 0);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const size_t s = (size_t)y * img.w + x, d = (size_t)(y + radius) * w + (x + radius);
            px[d] = img.px[s];
            inv[d] = img.xr[s] != 0;
        }

    const int r2 = radius * radius;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!inv[(size_t)y * w + x]) continue;
            px[(size_t)y * w + x] = kBlack;
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (dx * dx + dy * dy > r2 || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t j = (size_t)ny * w + nx;
                    if (!inv[j] && (px[j] >> 24) == 0) px[j] = kWhite;
                }
        }

    img.w = w; img.h = h;
    img.hot.x += radius; img.hot.y += radius;
    img.px.swap(px);
    img.xr.clear();
}

void XorLayer(const Image& img, const uint32_t* screen, uint32_t* out, uint32_t key)
{
    const size_t n = (size_t)img.w * img.h;
    for (size_t i = 0; i < n; ++i) {
        if (!img.inverts() || !img.xr[i]) { out[i] = key; continue; }
        const uint32_t c = (screen[i] ^ img.xr[i]) & 0x00FFFFFFu;
        out[i] = c == key ? (key ^ 1u) : c;
    }
}

int OutlineRadius(UINT dpi)
{
    return std::max(1, (int)std::lround(dpi / 96.0 * 0.8));
}

Image Scale(const Image& src, int zoomPercent)
{
    Image out;
    if (src.w <= 0 || src.h <= 0 || src.px.empty()) return out;
    if (zoomPercent < 1) zoomPercent = 1;

    const int k = std::max(1, zoomPercent / 100);
    const int mw = src.w * k, mh = src.h * k;   // the nearest-neighbour stage
    out.w = std::max(1, (src.w * zoomPercent + 50) / 100);
    out.h = std::max(1, (src.h * zoomPercent + 50) / 100);
    out.hot.x = (src.hot.x * zoomPercent + 50) / 100;
    out.hot.y = (src.hot.y * zoomPercent + 50) / 100;
    out.px.assign((size_t)out.w * out.h, 0);
    if (src.inverts()) out.xr.assign(out.px.size(), 0);

    // The nearest-neighbour image is never built: its pixel (x, y) is src(x/k, y/k).
    auto mid = [&](int x, int y) { return src.at(x / k, y / k); };
    auto midXr = [&](int x, int y) { return src.xr[(size_t)(y / k) * src.w + x / k]; };
    const double sxScale = (double)mw / out.w, syScale = (double)mh / out.h;

    for (int y = 0; y < out.h; ++y) {
        double sy = (y + 0.5) * syScale - 0.5;
        sy = std::clamp(sy, 0.0, (double)(mh - 1));
        const int y0 = (int)sy, y1 = std::min(y0 + 1, mh - 1);
        const double fy = sy - y0;
        for (int x = 0; x < out.w; ++x) {
            double sx = (x + 0.5) * sxScale - 0.5;
            sx = std::clamp(sx, 0.0, (double)(mw - 1));
            const int x0 = (int)sx, x1 = std::min(x0 + 1, mw - 1);
            const double fx = sx - x0;
            const uint32_t c00 = mid(x0, y0), c10 = mid(x1, y0), c01 = mid(x0, y1), c11 = mid(x1, y1);
            uint32_t v = 0;
            // Premultiplied channels interpolate without dark fringes at the edges.
            for (int sh = 0; sh < 32; sh += 8) {
                const double top = ((c00 >> sh) & 0xFF) * (1 - fx) + ((c10 >> sh) & 0xFF) * fx;
                const double bot = ((c01 >> sh) & 0xFF) * (1 - fx) + ((c11 >> sh) & 0xFF) * fx;
                const int ch = (int)(top * (1 - fy) + bot * fy + 0.5);
                v |= (uint32_t)std::min(255, ch) << sh;
            }
            out.px[(size_t)y * out.w + x] = v;

            if (src.inverts()) {
                // XOR pixels cannot be blended: an output pixel is one when they
                // cover at least half of it, and takes the strongest one's colour.
                const uint32_t xc[4] = { midXr(x0, y0), midXr(x1, y0), midXr(x0, y1), midXr(x1, y1) };
                const double wt[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
                double cover = 0, best = -1;
                uint32_t pick = 0;
                for (int c = 0; c < 4; ++c) {
                    if (!xc[c]) continue;
                    cover += wt[c];
                    if (wt[c] > best) { best = wt[c]; pick = xc[c]; }
                }
                if (cover >= 0.5) out.xr[(size_t)y * out.w + x] = pick;
            }
        }
    }
    return out;
}

int VisibleHeight(const Image& img)
{
    int top = -1, bottom = -1;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if ((img.at(x, y) >> 24) >= kVisibleAlpha || (img.inverts() && img.xr[(size_t)y * img.w + x])) {
                if (top < 0) top = y;
                bottom = y;
                break;
            }
    return top < 0 ? 0 : bottom - top + 1;
}

uint64_t Key(const Image& img)
{
    // FNV-1a over the geometry and the pixels.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; }
    };
    mix((uint32_t)img.w); mix((uint32_t)img.h);
    mix((uint32_t)img.hot.x); mix((uint32_t)img.hot.y);
    for (uint32_t c : img.px) mix(c);
    for (uint32_t c : img.xr) mix(c);
    return h;
}

bool Read(HCURSOR cursor, Image& out)
{
    ICONINFO ii{};
    if (!cursor || !GetIconInfo(cursor, &ii)) return false;

    bool ok = false;
    BITMAP bm{};
    HDC dc = GetDC(nullptr);
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        std::vector<uint32_t> color, mask;
        const int w = bm.bmWidth, h = bm.bmHeight;
        if (w > 0 && h > 0 && ReadBits(dc, ii.hbmColor, w, h, color) &&
            ii.hbmMask && ReadBits(dc, ii.hbmMask, w, h, mask)) {
            Decode(w, h, color.data(), mask.data(), POINT{ (LONG)ii.xHotspot, (LONG)ii.yHotspot }, out);
            ok = true;
        }
    } else if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof(bm), &bm)) {
        std::vector<uint32_t> mask;
        const int w = bm.bmWidth, h2 = bm.bmHeight;
        if (w > 0 && h2 >= 2 && ReadBits(dc, ii.hbmMask, w, h2, mask)) {
            Decode(w, h2 / 2, nullptr, mask.data(), POINT{ (LONG)ii.xHotspot, (LONG)ii.yHotspot }, out);
            ok = true;
        }
    }
    ReleaseDC(nullptr, dc);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);

    if (ok) {
        // A cursor with nothing visible (AnyDesk's "hidden remote cursor") has
        // nothing to enlarge; the caller then leaves the original alone.
        ok = out.inverts() ||
             std::any_of(out.px.begin(), out.px.end(), [](uint32_t c) { return (c >> 24) != 0; });
    }
    return ok;
}

} // namespace cursorimg
