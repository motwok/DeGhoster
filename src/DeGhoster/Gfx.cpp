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

#include "Gfx.h"

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace G = Gdiplus;

namespace gfx {

void DrawGlyph(HDC hdc, RECT rc, const wchar_t* glyph, COLORREF color, int pxSize)
{
    G::Graphics g(hdc);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
    G::FontFamily ff(L"Segoe MDL2 Assets");
    G::Font f(&ff, (G::REAL)pxSize, G::FontStyleRegular, G::UnitPixel);
    G::SolidBrush br(G::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    G::StringFormat sf;
    sf.SetAlignment(G::StringAlignmentCenter);
    sf.SetLineAlignment(G::StringAlignmentCenter);
    G::RectF r((G::REAL)rc.left, (G::REAL)rc.top,
               (G::REAL)(rc.right - rc.left), (G::REAL)(rc.bottom - rc.top));
    g.DrawString(glyph, 1, &f, r, &sf, &br);
}

void FillCircle(HDC hdc, RECT rc, BYTE alpha, COLORREF color)
{
    G::Graphics g(hdc);
    g.SetSmoothingMode(G::SmoothingModeAntiAlias);
    G::SolidBrush b(G::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    g.FillEllipse(&b, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

GdiPlus::GdiPlus()
{
    G::GdiplusStartupInput in;
    G::GdiplusStartup(&token_, &in, nullptr);
}

GdiPlus::~GdiPlus()
{
    G::GdiplusShutdown(token_);
}

} // namespace gfx
