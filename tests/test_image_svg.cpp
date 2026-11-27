// Our own SVG renderer (Core/ImageDecodeSvg.cpp, from the W3C SVG 1.1 / SVG 2 specifications). Every document is
// written here by hand; the expected pixels are worked out from the geometry (areas, positions, colours), not from
// another renderer. Covers sizing (width / height / viewBox / preserveAspectRatio), the path grammar, shapes,
// transforms, fill rules, strokes (caps, joins, dashes), gradients, opacity, CSS (<style>, style="", presentation
// attributes), currentColor, use / symbol, clip paths, nested svg, DOCTYPE entities, detection, and hostile documents
// (exponential <use>, deep nesting, huge numbers, truncation, mutation) that must end quickly and cleanly.
#include "test_framework.hpp"
#include "okay/Core/ImageDecode.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using okay::DecodedImage;

namespace {

bool Render(const std::string& svg, DecodedImage& img, int w = 0, int h = 0, std::string* err = nullptr) {
    const auto* d = reinterpret_cast<const std::uint8_t*>(svg.data());
    if (w || h) return okay::RasterizeSvg(d, svg.size(), w, h, img, err);
    return okay::DecodeImage(d, svg.size(), img, err);
}

// A pixel; a transparent black stand-in outside the picture (or when a render failed), so a failure is reported, not a crash.
const std::uint8_t* Px(const DecodedImage& img, int x, int y) {
    static const std::uint8_t kNone[4] = {0, 0, 0, 0};
    if (x < 0 || y < 0 || x >= img.width || y >= img.height || img.rgba.size() < std::size_t(img.width) * std::size_t(img.height) * 4) return kNone;
    return &img.rgba[(std::size_t(y) * std::size_t(img.width) + std::size_t(x)) * 4];
}
int A(const DecodedImage& img, int x, int y) { return Px(img, x, y)[3]; }
bool Is(const DecodedImage& img, int x, int y, int r, int g, int b, int a, int tol = 1) {
    const std::uint8_t* p = Px(img, x, y);
    return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol && std::abs(p[3] - a) <= tol;
}
// Covered area in pixels (alpha summed / 255).
double Area(const DecodedImage& img) {
    double s = 0;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) s += img.rgba[i] / 255.0;
    return s;
}
std::string Doc(int w, int h, const std::string& body, const std::string& extra = "") {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h) + "\" " + extra + ">" + body + "</svg>";
}
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

} // namespace

int main() {
    RUN_SUITE("image_svg");
    DecodedImage img;

    // ---- sizing ----------------------------------------------------------------------------------------------------
    {
        CHECK(Render(Doc(8, 6, ""), img) && img.width == 8 && img.height == 6 && Area(img) == 0);
        CHECK(Render("<svg viewBox=\"0 0 24 16\"/>", img) && img.width == 24 && img.height == 16);
        CHECK(Render("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>", img) && img.width == 300 && img.height == 150);
        CHECK(Render("<svg width=\"1in\" height=\"25.4mm\"/>", img) && img.width == 96 && img.height == 96);
        CHECK(Render("<svg width=\"100%\" height=\"100%\" viewBox=\"0 0 40 20\"/>", img) && img.width == 40 && img.height == 20);
        CHECK(Render("<svg width=\"50\" viewBox=\"0 0 40 20\"/>", img) && img.width == 50 && img.height == 25);
        CHECK(Render("<svg width=\"90000\" height=\"10\"/>", img) && img.width == 4096 && img.height == 1);   // at most 4096 a side
        CHECK(Render(Doc(10, 10, ""), img, 64, 0) && img.width == 64 && img.height == 64);                   // requested size
        int w = 0, h = 0;
        okay::ImageFormat f = okay::ImageFormat::Unknown;
        const std::string s = "<?xml version=\"1.0\"?>\n<!-- a comment -->\n<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"x\">\n" + Doc(33, 7, "");
        CHECK(okay::GetImageInfo(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), &w, &h, &f) && w == 33 && h == 7 && f == okay::ImageFormat::SVG);
        const std::string html = "<!DOCTYPE html><html><body><svg width=\"5\" height=\"5\"></svg></body></html>";
        CHECK(okay::DetectImageFormat(reinterpret_cast<const std::uint8_t*>(html.data()), html.size()) == okay::ImageFormat::Unknown);
        const std::string other = "<?xml version=\"1.0\"?><model unit=\"mm\"/>";
        CHECK(okay::DetectImageFormat(reinterpret_cast<const std::uint8_t*>(other.data()), other.size()) == okay::ImageFormat::Unknown);
    }

    // ---- solid fills land on exact pixels ---------------------------------------------------------------------------
    {
        CHECK(Render(Doc(8, 6, "<rect x=\"2\" y=\"1\" width=\"3\" height=\"2\" fill=\"#ff0000\"/>"), img));
        int inside = 0, outside = 0;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 8; ++x) {
                const bool in = x >= 2 && x < 5 && y >= 1 && y < 3;
                if (in ? Is(img, x, y, 255, 0, 0, 255, 0) : A(img, x, y) == 0) (in ? inside : outside)++;
            }
        CHECK(inside == 6 && outside == 42);
        // a half-pixel edge gives half coverage
        CHECK(Render(Doc(4, 2, "<rect x=\"0.5\" y=\"0\" width=\"2\" height=\"2\" fill=\"blue\"/>"), img));
        CHECK(std::abs(A(img, 0, 0) - 128) <= 1 && A(img, 1, 0) == 255 && std::abs(A(img, 2, 0) - 128) <= 1 && A(img, 3, 0) == 0);
        CHECK(Is(img, 0, 0, 0, 0, 255, 128));
        // colour syntaxes
        CHECK(Render(Doc(4, 1, "<rect width=\"1\" height=\"1\" fill=\"rgb(10,20,30)\"/><rect x=\"1\" width=\"1\" height=\"1\" fill=\"#abc\"/>"
                               "<rect x=\"2\" width=\"1\" height=\"1\" fill=\"hsl(120,100%,50%)\"/><rect x=\"3\" width=\"1\" height=\"1\" fill=\"rebeccapurple\"/>"), img));
        CHECK(Is(img, 0, 0, 10, 20, 30, 255, 0) && Is(img, 1, 0, 0xAA, 0xBB, 0xCC, 255, 0) && Is(img, 2, 0, 0, 255, 0, 255, 0) && Is(img, 3, 0, 0x66, 0x33, 0x99, 255, 0));
        // default fill is black, fill="none" draws nothing
        CHECK(Render(Doc(2, 1, "<rect width=\"1\" height=\"1\"/><rect x=\"1\" width=\"1\" height=\"1\" fill=\"none\"/>"), img));
        CHECK(Is(img, 0, 0, 0, 0, 0, 255, 0) && A(img, 1, 0) == 0);
    }

    // ---- areas of curved shapes and path grammar -----------------------------------------------------------------
    {
        CHECK(Render(Doc(100, 100, "<circle cx=\"50\" cy=\"50\" r=\"40\"/>"), img));
        CHECK(std::fabs(Area(img) - 3.14159265 * 1600) < 3.14159265 * 1600 * 0.003);
        CHECK(Render(Doc(100, 100, "<ellipse cx=\"50\" cy=\"50\" rx=\"40\" ry=\"20\"/>"), img));
        CHECK(std::fabs(Area(img) - 3.14159265 * 800) < 3.14159265 * 800 * 0.003);
        // the same circle as two arcs, the second with the compact flag syntax ("10-80" is the flags 1, 0 and then -80)
        CHECK(Render(Doc(100, 100, "<path d=\"M10 50a40,40,0,1,0,80,0a40 40 0 10-80 0z\"/>"), img));
        CHECK(std::fabs(Area(img) - 3.14159265 * 1600) < 3.14159265 * 1600 * 0.003);
        // rounded rectangle: area w h - (4 - pi) r^2
        CHECK(Render(Doc(100, 100, "<rect x=\"10\" y=\"10\" width=\"60\" height=\"40\" rx=\"10\"/>"), img));
        CHECK(std::fabs(Area(img) - (2400 - (4 - 3.14159265) * 100)) < 3);
        // relative commands, implicit lineto, H / V, compact numbers: a 20 x 10 rectangle
        CHECK(Render(Doc(40, 40, "<path d=\"m5 5 20 0v10H5z\"/>"), img));
        CHECK(std::fabs(Area(img) - 200) < 0.5);
        CHECK(Render(Doc(40, 40, "<path d=\"M5,5L25,5L25,15L5,15Z M30-1.5.5.5\"/>"), img));   // a broken tail is ignored
        CHECK(std::fabs(Area(img) - 200) < 0.5);
        CHECK(Render(Doc(40, 40, "<path d=\"M5 5h2e1v1E1h-20z\"/>"), img));
        CHECK(std::fabs(Area(img) - 200) < 0.5);
        // smooth curves: S reflects C's control point, T reflects Q's; a symmetric bump has the area of its parabola
        // Q from (0,20) with control (10,0) to (20,20): area between chord and curve = 2/3 * 20 * 10
        CHECK(Render(Doc(40, 40, "<path d=\"M0 20Q10 0 20 20z\"/>"), img));
        CHECK(std::fabs(Area(img) - 400.0 / 3) < 0.5);
        CHECK(Render(Doc(40, 40, "<path d=\"M0 20Q10 0 20 20T40 20z\"/>"), img));   // the second hump hangs down: net areas
        CHECK(std::fabs(Area(img) - 2 * 400.0 / 3) < 1);
        CHECK(Render(Doc(40, 40, "<path d=\"M0 20C0 0 20 0 20 20S40 40 40 20z\"/>"), img));
        // each hump: x(t) = 20 (3t^2 - 2t^3), height 60 t (1 - t), area = integral of 60t(1-t) 20(6t - 6t^2) dt = 240
        const double hump = 240;
        CHECK(std::fabs(Area(img) - 2 * hump) < 1.5);
        // polygon and polyline
        CHECK(Render(Doc(20, 20, "<polygon points=\"0,0 10,0 10,10\"/>"), img) && std::fabs(Area(img) - 50) < 0.5);
        CHECK(Render(Doc(20, 20, "<polyline points=\"0 0 10 0 10 10\"/>"), img) && std::fabs(Area(img) - 50) < 0.5);   // filled as if closed
        CHECK(Render(Doc(20, 20, "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\"/>"), img) && Area(img) == 0);           // no stroke: nothing
    }

    // ---- fill rules ----------------------------------------------------------------------------------------------
    {
        const std::string squares = "M0 0h20v20h-20z M5 5h10v10h-10z";
        CHECK(Render(Doc(20, 20, "<path d=\"" + squares + "\"/>"), img) && std::fabs(Area(img) - 400) < 0.5);
        CHECK(Render(Doc(20, 20, "<path fill-rule=\"evenodd\" d=\"" + squares + "\"/>"), img) && std::fabs(Area(img) - 300) < 0.5);
        CHECK(A(img, 10, 10) == 0 && A(img, 2, 2) == 255);
        CHECK(Render(Doc(20, 20, "<path d=\"M0 0h20v20h-20z M5 5v10h10v-10z\"/>"), img) && std::fabs(Area(img) - 300) < 0.5);   // opposite winding
    }

    // ---- transforms and viewports --------------------------------------------------------------------------------
    {
        CHECK(Render(Doc(10, 10, "<rect width=\"2\" height=\"2\" transform=\"translate(3 4)\"/>"), img));
        CHECK(A(img, 3, 4) == 255 && A(img, 4, 5) == 255 && A(img, 2, 4) == 0 && A(img, 5, 4) == 0);
        CHECK(Render(Doc(10, 10, "<rect width=\"2\" height=\"1\" transform=\"scale(2,3)\"/>"), img) && std::fabs(Area(img) - 12) < 0.01);
        CHECK(Render(Doc(10, 10, "<rect width=\"4\" height=\"1\" transform=\"rotate(90 5 5)\"/>"), img));   // maps x 0..4, y 0..1 to x 9..10?..
        // rotate(90) about (5,5): (x, y) -> (10 - y, x); the rect becomes x 9..10, y 0..4
        CHECK(A(img, 9, 0) == 255 && A(img, 9, 3) == 255 && A(img, 8, 0) == 0 && A(img, 9, 4) == 0);
        CHECK(Render(Doc(10, 10, "<g transform=\"matrix(1 0 0 1 2 0)\"><rect width=\"1\" height=\"1\" transform=\"translate(1)\"/></g>"), img) && A(img, 3, 0) == 255);
        CHECK(Render(Doc(10, 10, "<rect width=\"2\" height=\"2\" transform=\"skewX(45)\"/>"), img) && std::fabs(Area(img) - 4) < 0.01);
        // viewBox, preserveAspectRatio: a 10 x 10 view in a 20 x 10 picture is centred (xMidYMid meet): x offset 5
        CHECK(Render(Doc(20, 10, "<rect width=\"10\" height=\"10\"/>", "viewBox=\"0 0 10 10\""), img));
        CHECK(A(img, 4, 5) == 0 && A(img, 5, 5) == 255 && A(img, 14, 5) == 255 && A(img, 15, 5) == 0);
        CHECK(Render(Doc(20, 10, "<rect width=\"10\" height=\"10\"/>", "viewBox=\"0 0 10 10\" preserveAspectRatio=\"xMinYMin meet\""), img));
        CHECK(A(img, 0, 5) == 255 && A(img, 10, 5) == 0);
        CHECK(Render(Doc(20, 10, "<rect width=\"10\" height=\"10\"/>", "viewBox=\"0 0 10 10\" preserveAspectRatio=\"none\""), img));
        CHECK(A(img, 19, 5) == 255 && std::fabs(Area(img) - 200) < 0.01);
        CHECK(Render(Doc(20, 10, "<rect width=\"10\" height=\"5\"/>", "viewBox=\"0 0 10 10\" preserveAspectRatio=\"xMidYMid slice\""), img));
        CHECK(std::fabs(Area(img) - 100) < 0.01);   // scale 2, cut to the picture: only the top half of 20 x 20 shows
        CHECK(Render(Doc(20, 10, "<rect width=\"10\" height=\"10\"/>", "viewBox=\"0 0 0 10\""), img) && Area(img) == 0);   // zero viewBox: nothing
        // nested svg with its own viewport
        CHECK(Render(Doc(20, 20, "<svg x=\"10\" y=\"0\" width=\"10\" height=\"10\" viewBox=\"0 0 1 1\"><rect width=\"1\" height=\"1\"/></svg>"), img));
        CHECK(A(img, 10, 0) == 255 && A(img, 19, 9) == 255 && A(img, 9, 0) == 0 && std::fabs(Area(img) - 100) < 0.01);
        // a requested size scales everything
        CHECK(Render(Doc(10, 10, "<rect width=\"5\" height=\"10\"/>"), img, 40, 40) && std::fabs(Area(img) - 800) < 0.01);
    }

    // ---- strokes -------------------------------------------------------------------------------------------------
    {
        CHECK(Render(Doc(20, 20, "<line x1=\"5\" y1=\"10\" x2=\"15\" y2=\"10\" stroke=\"black\" stroke-width=\"2\"/>"), img));
        CHECK(std::fabs(Area(img) - 20) < 0.05 && A(img, 5, 9) == 255 && A(img, 14, 10) == 255 && A(img, 4, 10) == 0 && A(img, 15, 10) == 0);
        CHECK(Render(Doc(20, 20, "<line x1=\"5\" y1=\"10\" x2=\"15\" y2=\"10\" stroke=\"black\" stroke-width=\"2\" stroke-linecap=\"square\"/>"), img));
        CHECK(std::fabs(Area(img) - 24) < 0.05 && A(img, 4, 10) == 255 && A(img, 15, 10) == 255);
        CHECK(Render(Doc(20, 20, "<line x1=\"5\" y1=\"10\" x2=\"15\" y2=\"10\" stroke=\"black\" stroke-width=\"2\" stroke-linecap=\"round\"/>"), img));
        CHECK(std::fabs(Area(img) - (20 + 3.14159265)) < 0.1);
        // a closed square outline: 10 x 10 square, width 2: outer 12 x 12 minus inner 8 x 8 (miter corners)
        CHECK(Render(Doc(20, 20, "<rect x=\"4\" y=\"4\" width=\"10\" height=\"10\" fill=\"none\" stroke=\"red\" stroke-width=\"2\"/>"), img));
        CHECK(std::fabs(Area(img) - 80) < 0.1 && Is(img, 3, 3, 255, 0, 0, 255, 0) && A(img, 8, 8) == 0);
        // bevel corners cut half of each 1 x 1 corner square
        CHECK(Render(Doc(20, 20, "<rect x=\"4\" y=\"4\" width=\"10\" height=\"10\" fill=\"none\" stroke=\"red\" stroke-width=\"2\" stroke-linejoin=\"bevel\"/>"), img));
        CHECK(std::fabs(Area(img) - 78) < 0.1);
        CHECK(Render(Doc(20, 20, "<rect x=\"4\" y=\"4\" width=\"10\" height=\"10\" fill=\"none\" stroke=\"red\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"), img));
        CHECK(std::fabs(Area(img) - (76 + 3.14159265)) < 0.15);
        // a 90 degree miter is sqrt(2) long: a miter limit of 1.4 turns it into a bevel
        CHECK(Render(Doc(20, 20, "<rect x=\"4\" y=\"4\" width=\"10\" height=\"10\" fill=\"none\" stroke=\"red\" stroke-width=\"2\" stroke-miterlimit=\"1.4\"/>"), img));
        CHECK(std::fabs(Area(img) - 78) < 0.1);
        // dashes: a 20 long line with "4 2" -> dashes 0-4, 6-10, 12-16, 18-20: 14 units, width 1 (row 2 exactly)
        CHECK(Render(Doc(30, 4, "<line x1=\"0\" y1=\"2.5\" x2=\"20\" y2=\"2.5\" stroke=\"black\" stroke-dasharray=\"4 2\"/>"), img));
        CHECK(std::fabs(Area(img) - 14) < 0.05 && A(img, 1, 2) == 255 && A(img, 4, 2) == 0 && A(img, 7, 2) == 255 && A(img, 19, 2) == 255);
        CHECK(Render(Doc(30, 4, "<line x1=\"0\" y1=\"2.5\" x2=\"20\" y2=\"2.5\" stroke=\"black\" stroke-dasharray=\"4,2\" stroke-dashoffset=\"4\"/>"), img));
        CHECK(std::fabs(Area(img) - 12) < 0.05 && A(img, 0, 2) == 0 && A(img, 2, 2) == 255);
        CHECK(Render(Doc(30, 4, "<line x1=\"0\" y1=\"2.5\" x2=\"20\" y2=\"2.5\" stroke=\"black\" stroke-dasharray=\"3\"/>"), img));   // odd list repeats
        CHECK(std::fabs(Area(img) - 11) < 0.05);
        // stroke width scales with the transform; zero-length subpaths get round caps as dots
        CHECK(Render(Doc(20, 20, "<line x1=\"0\" y1=\"5\" x2=\"10\" y2=\"5\" stroke=\"black\" transform=\"scale(2)\"/>"), img) && std::fabs(Area(img) - 40) < 0.05);
        CHECK(Render(Doc(20, 20, "<path d=\"M10 10z\" stroke=\"black\" stroke-width=\"4\" stroke-linecap=\"round\"/>"), img));
        CHECK(std::fabs(Area(img) - 4 * 3.14159265) < 0.2);
        CHECK(Render(Doc(20, 20, "<path d=\"M10 10\" stroke=\"black\" stroke-width=\"4\" stroke-linecap=\"round\"/>"), img) && Area(img) == 0);
    }

    // ---- paint: opacity, currentColor, gradients ------------------------------------------------------------------
    {
        CHECK(Render(Doc(2, 1, "<rect width=\"1\" height=\"1\" fill=\"#fff\" fill-opacity=\"0.5\"/><rect x=\"1\" width=\"1\" height=\"1\" fill=\"#fff\" opacity=\".25\"/>"), img));
        CHECK(Is(img, 0, 0, 255, 255, 255, 128) && Is(img, 1, 0, 255, 255, 255, 64));
        CHECK(Render(Doc(3, 1, "<g opacity=\"0.5\"><rect width=\"1\" height=\"1\" fill=\"red\" opacity=\"0.5\"/></g><rect x=\"1\" width=\"1\" height=\"1\" fill=\"rgba(0,0,255,0.5)\"/>"
                               "<rect x=\"2\" width=\"1\" height=\"1\" fill=\"#000\"/><rect x=\"2\" width=\"1\" height=\"1\" fill=\"#fff\" fill-opacity=\"0.5\"/>"), img));
        CHECK(A(img, 0, 0) == 64 && Is(img, 1, 0, 0, 0, 255, 128) && Is(img, 2, 0, 128, 128, 128, 255));   // source-over
        CHECK(Render(Doc(2, 1, "<g color=\"#123456\"><rect width=\"1\" height=\"1\" fill=\"currentColor\"/></g><rect x=\"1\" width=\"1\" height=\"1\" style=\"color:lime;fill:currentcolor\"/>"), img));
        CHECK(Is(img, 0, 0, 0x12, 0x34, 0x56, 255, 0) && Is(img, 1, 0, 0, 255, 0, 255, 0));
        // a horizontal linear gradient red -> blue over 256 pixels (bounding-box units)
        const std::string lin = "<defs><linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"#f00\"/><stop offset=\"1\" stop-color=\"#00f\"/></linearGradient></defs>";
        CHECK(Render(Doc(256, 2, lin + "<rect width=\"256\" height=\"2\" fill=\"url(#g)\"/>"), img));
        CHECK(Is(img, 0, 0, 255, 0, 0, 255, 2) && Is(img, 255, 0, 0, 0, 255, 255, 2) && Is(img, 128, 1, 127, 0, 128, 255, 2));
        // userSpaceOnUse, a transform, reflect and repeat
        CHECK(Render(Doc(100, 1, "<linearGradient id=\"r\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" x2=\"25\" spreadMethod=\"repeat\">"
                                 "<stop offset=\"0\" stop-color=\"black\"/><stop offset=\"1\" stop-color=\"white\"/></linearGradient>"
                                 "<rect width=\"100\" height=\"1\" fill=\"url(#r)\"/>"), img));
        CHECK(Px(img, 1, 0)[0] < 20 && Px(img, 24, 0)[0] > 235 && Px(img, 26, 0)[0] < 20 && Px(img, 74, 0)[0] > 235);
        CHECK(Render(Doc(100, 1, "<linearGradient id=\"r\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" x2=\"25\" spreadMethod=\"reflect\">"
                                 "<stop offset=\"0\" stop-color=\"black\"/><stop offset=\"1\" stop-color=\"white\"/></linearGradient>"
                                 "<rect width=\"100\" height=\"1\" fill=\"url(#r)\"/>"), img));
        CHECK(Px(img, 24, 0)[0] > 235 && Px(img, 26, 0)[0] > 235 && Px(img, 49, 0)[0] < 20);
        CHECK(Render(Doc(100, 1, "<linearGradient id=\"r\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" x2=\"25\">"
                                 "<stop offset=\"0\" stop-color=\"black\"/><stop offset=\"1\" stop-color=\"white\"/></linearGradient>"
                                 "<rect width=\"100\" height=\"1\" fill=\"url(#r)\"/>"), img));
        CHECK(Px(img, 80, 0)[0] == 255);   // pad
        // stops inherited through href, a vertical gradient via gradientTransform, stop-opacity, CSS-styled stops
        CHECK(Render(Doc(1, 100, "<style>.s1{stop-color:#00ff00}</style><linearGradient id=\"base\"><stop offset=\"0\" class=\"s1\"/>"
                                 "<stop offset=\"1\" stop-color=\"#00ff00\" stop-opacity=\"0\"/></linearGradient>"
                                 "<linearGradient id=\"v\" href=\"#base\" gradientTransform=\"rotate(90)\"/>"
                                 "<rect width=\"1\" height=\"100\" fill=\"url(#v)\"/>"), img));
        CHECK(Is(img, 0, 0, 0, 255, 0, 252, 3) && A(img, 0, 99) < 5 && std::abs(A(img, 0, 50) - 125) < 4);
        // radial: the centre has the first stop's colour, the rim the last; a missing paint server uses the fallback
        CHECK(Render(Doc(41, 41, "<radialGradient id=\"rg\"><stop offset=\"0\" stop-color=\"white\"/><stop offset=\"1\" stop-color=\"black\"/></radialGradient>"
                                 "<rect width=\"41\" height=\"41\" fill=\"url(#rg)\"/>"), img));
        CHECK(Px(img, 20, 20)[0] > 245 && Px(img, 0, 20)[0] < 10 && Px(img, 0, 0)[0] < 10 && std::abs(Px(img, 10, 20)[0] - 128) < 12);
        CHECK(Render(Doc(2, 1, "<rect width=\"1\" height=\"1\" fill=\"url(#nothing) #0000ff\"/><rect x=\"1\" width=\"1\" height=\"1\" fill=\"url(#nothing)\"/>"), img));
        CHECK(Is(img, 0, 0, 0, 0, 255, 255, 0) && A(img, 1, 0) == 0);
    }

    // ---- CSS -------------------------------------------------------------------------------------------------------
    {
        const std::string sheet = "<style><![CDATA[ /* rules */ rect{fill:#010101} .a{fill:#00ff00} rect.b{fill:#0000ff} #c{fill:#ff00ff}"
                                  " @media print { rect { fill: red } } g > rect{fill:red} .a:hover{fill:red} ]]></style>";
        CHECK(Render(Doc(6, 1, sheet + "<rect width=\"1\" height=\"1\" fill=\"red\"/><rect x=\"1\" width=\"1\" height=\"1\" class=\"a\"/>"
                                       "<rect x=\"2\" width=\"1\" height=\"1\" class=\"a b\"/><rect x=\"3\" width=\"1\" height=\"1\" class=\"b\" id=\"c\"/>"
                                       "<rect x=\"4\" width=\"1\" height=\"1\" class=\"a\" style=\"fill: #ffff00\"/><g><rect x=\"5\" width=\"1\" height=\"1\"/></g>"), img));
        CHECK(Is(img, 0, 0, 1, 1, 1, 255, 0));          // a sheet rule beats the presentation attribute
        CHECK(Is(img, 1, 0, 0, 255, 0, 255, 0));        // class
        CHECK(Is(img, 2, 0, 0, 0, 255, 255, 0));        // tag.class is more specific than .class
        CHECK(Is(img, 3, 0, 255, 0, 255, 255, 0));      // id
        CHECK(Is(img, 4, 0, 255, 255, 0, 255, 0));      // style="" wins
        CHECK(Is(img, 5, 0, 1, 1, 1, 255, 0));          // combinators, pseudo-classes and @media are ignored
        // inheritance: a group's fill reaches its children; display:none hides a subtree, visibility:hidden one shape
        CHECK(Render(Doc(4, 1, "<g fill=\"#0000ff\"><rect width=\"1\" height=\"1\"/><g style=\"display:none\"><rect x=\"1\" width=\"1\" height=\"1\"/></g>"
                               "<rect x=\"2\" width=\"1\" height=\"1\" visibility=\"hidden\"/><g visibility=\"hidden\"><rect x=\"3\" width=\"1\" height=\"1\" visibility=\"visible\"/></g></g>"), img));
        CHECK(Is(img, 0, 0, 0, 0, 255, 255, 0) && A(img, 1, 0) == 0 && A(img, 2, 0) == 0 && A(img, 3, 0) == 255);
    }

    // ---- structure: use, symbol, clip paths, switch, entities --------------------------------------------------------
    {
        CHECK(Render(Doc(10, 10, "<defs><rect id=\"r\" width=\"2\" height=\"2\"/></defs><use href=\"#r\" x=\"3\" y=\"3\" fill=\"red\"/>"
                                 "<use xlink:href=\"#r\" x=\"6\" y=\"6\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"/>"), img));
        CHECK(Is(img, 3, 3, 255, 0, 0, 255, 0) && Is(img, 6, 6, 0, 0, 0, 255, 0) && A(img, 0, 0) == 0 && std::fabs(Area(img) - 8) < 0.01);
        CHECK(Render(Doc(20, 20, "<symbol id=\"s\" viewBox=\"0 0 1 1\"><rect width=\"1\" height=\"1\"/></symbol><use href=\"#s\" x=\"10\" width=\"5\" height=\"5\"/>"), img));
        CHECK(A(img, 10, 0) == 255 && A(img, 14, 4) == 255 && A(img, 15, 4) == 0 && std::fabs(Area(img) - 25) < 0.01);
        // a clip path: a 20 x 20 square clipped by a circle of radius 10 leaves the circle
        CHECK(Render(Doc(40, 40, "<clipPath id=\"c\"><circle cx=\"20\" cy=\"20\" r=\"10\"/></clipPath><rect width=\"40\" height=\"40\" clip-path=\"url(#c)\"/>"), img));
        CHECK(std::fabs(Area(img) - 3.14159265 * 100) < 2);
        CHECK(Render(Doc(40, 40, "<clipPath id=\"c\" clipPathUnits=\"objectBoundingBox\"><rect width=\"0.5\" height=\"1\"/></clipPath>"
                                 "<rect x=\"10\" width=\"20\" height=\"10\" clip-path=\"url(#c)\"/>"), img));
        CHECK(std::fabs(Area(img) - 100) < 0.5 && A(img, 12, 5) == 255 && A(img, 25, 5) == 0);
        CHECK(Render(Doc(40, 40, "<clipPath id=\"c\"><rect width=\"5\" height=\"40\"/></clipPath><g clip-path=\"url(#c)\"><rect width=\"40\" height=\"40\"/></g>"), img));
        CHECK(std::fabs(Area(img) - 200) < 0.5);
        CHECK(Render(Doc(4, 1, "<switch><rect width=\"1\" height=\"1\"/><rect x=\"2\" width=\"1\" height=\"1\"/></switch>"), img) && A(img, 0, 0) == 255 && A(img, 2, 0) == 0);
        const std::string ent = "<?xml version=\"1.0\"?><!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"x\" [<!ENTITY ns \"http://www.w3.org/2000/svg\">"
                                "<!ENTITY red \"fill:#ff0000;\"><!-- note --><!ATTLIST svg a CDATA #IMPLIED>]><svg xmlns=\"&ns;\" width=\"2\" height=\"1\">"
                                "<rect width=\"1\" height=\"1\" style=\"&red;\"/><rect x=\"1\" width=\"1\" height=\"1\" fill=\"#00f\" data-x=\"&amp;\"/></svg>";
        CHECK(Render(ent, img) && Is(img, 0, 0, 255, 0, 0, 255, 0) && Is(img, 1, 0, 0, 0, 255, 255, 0));
        std::string err;
        CHECK(!Render("<!DOCTYPE svg [<!ENTITY % p \"x\">]><svg/>", img, 0, 0, &err) && !err.empty());
        CHECK(!Render("<!DOCTYPE svg [<!ENTITY e SYSTEM \"file:///etc/passwd\">]><svg>&e;</svg>", img));
        CHECK(!Render("<!DOCTYPE svg [<!ENTITY a \"&b;\"><!ENTITY b \"x\">]><svg/>", img));
        // things we do not draw are skipped, not fatal
        CHECK(Render(Doc(10, 10, "<text x=\"1\" y=\"5\">hi</text><image href=\"x.png\" width=\"5\" height=\"5\"/><filter id=\"f\"/><rect width=\"1\" height=\"1\" filter=\"url(#f)\"/>"), img));
        CHECK(std::fabs(Area(img) - 1) < 0.01);
    }

    // ---- hostile documents ---------------------------------------------------------------------------------------
    {
        // exponential <use>: each level uses the previous one ten times (10^12 shapes if expanded)
        std::string bomb = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\"><defs><rect id=\"l0\" width=\"256\" height=\"256\"/>";
        for (int i = 1; i <= 12; ++i) {
            bomb += "<g id=\"l" + std::to_string(i) + "\">";
            for (int k = 0; k < 10; ++k) bomb += "<use href=\"#l" + std::to_string(i - 1) + "\"/>";
            bomb += "</g>";
        }
        bomb += "</defs><use href=\"#l12\"/></svg>";
        double t0 = Now();
        CHECK(Render(bomb, img) && img.width == 256);
        const double tBomb = Now() - t0;
        // a use that points at its own ancestor
        t0 = Now();
        CHECK(Render(Doc(50, 50, "<g id=\"a\"><rect width=\"5\" height=\"5\"/><use href=\"#a\" x=\"1\"/><use href=\"#a\" y=\"1\"/></g>"), img));
        const double tCycle = Now() - t0;
        // the same with a clip path on the repeated group of a large picture: every copy needs a whole-picture mask
        // (found by the fuzzer: 18 s under ASan before masks counted as work)
        t0 = Now();
        CHECK(Render("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1024\" height=\"1376\"><clipPath id=\"c\"><circle r=\"298\"/></clipPath>"
                     "<g id=\"a\" clip-path=\"url(#c)\"><path d=\"m-11 180v118h22v-118\"/><use href=\"#a\" transform=\"rotate(45)\"/>"
                     "<use href=\"#a\" transform=\"rotate(90)\"/><use href=\"#a\" transform=\"rotate(135)\"/></g></svg>", img));
        const double tClip = Now() - t0;
        // deep nesting, a huge path, huge numbers, NaN-like values
        std::string deep = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">";
        for (int i = 0; i < 120; ++i) deep += "<g>";
        deep += "<rect width=\"5\" height=\"5\"/>";
        for (int i = 0; i < 120; ++i) deep += "</g>";
        deep += "</svg>";
        CHECK(Render(deep, img));   // drawn or not, it must not crash
        std::string big = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\"><path d=\"M0 0";
        for (int i = 0; i < 200000; ++i) big += " L" + std::to_string(i % 100) + " " + std::to_string((i * 7) % 100);
        big += "z\" stroke=\"black\" stroke-dasharray=\"0.001 0.001\"/></svg>";
        t0 = Now();
        CHECK(Render(big, img));
        const double tBig = Now() - t0;
        CHECK(Render(Doc(10, 10, "<rect width=\"1e308\" height=\"1e308\" x=\"-1e308\"/><circle r=\"1e400\"/><path d=\"M1e999 0L0 1e-999z\" stroke-width=\"1e30\" stroke=\"red\"/>"
                                 "<rect width=\"5\" height=\"5\" transform=\"scale(1e-300)\"/><rect width=\"5\" height=\"5\" transform=\"rotate(1e300)\"/>"), img));
        std::printf("  hostile: use bomb %.2f s, use cycle %.2f s, clipped use cycle %.2f s, 200000-segment dashed path %.2f s\n", tBomb, tCycle, tClip,
                    tBig);
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
        const double lim = 60;
#else
        const double lim = 10;
#endif
        CHECK(tBomb < lim && tCycle < lim && tClip < lim && tBig < lim);
        // truncations and mutations of a document using most features
        const std::string full = "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\" viewBox=\"0 0 80 60\">"
                                 "<style>.k{fill:#456;stroke:red}</style><defs><linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"red\"/>"
                                 "<stop offset=\"1\" stop-color=\"blue\"/></linearGradient><clipPath id=\"c\"><circle cx=\"40\" cy=\"30\" r=\"25\"/></clipPath>"
                                 "<symbol id=\"s\" viewBox=\"0 0 2 2\"><path d=\"M0 0h2l-1 2z\"/></symbol></defs>"
                                 "<g clip-path=\"url(#c)\" opacity=\".8\"><rect class=\"k\" x=\"5\" y=\"5\" width=\"70\" height=\"50\" rx=\"8\"/>"
                                 "<path d=\"M10 50C20 10 60 10 70 50S10 30 40 20a10 5 30 1 1 5 5z\" fill=\"url(#g)\" stroke-dasharray=\"3 1\"/></g>"
                                 "<use href=\"#s\" x=\"60\" y=\"40\" width=\"10\" height=\"10\"/><polyline points=\"1,1 5,9 9,1\" stroke=\"#0f0\" fill=\"none\"/></svg>";
        CHECK(Render(full, img) && img.width == 40 && Area(img) > 100);
        int ok = 0, n = 0;
        for (std::size_t len = 0; len < full.size(); ++len) {
            DecodedImage t;
            std::string e;
            if (Render(full.substr(0, len), t, 0, 0, &e)) { ++ok; CHECK(t.rgba.size() == std::size_t(t.width) * std::size_t(t.height) * 4); }
            else CHECK(!e.empty() && t.rgba.empty());
            ++n;
        }
        std::uint64_t r = 12345;
        for (int i = 0; i < 4000; ++i) {
            std::string m = full;
            for (int k = 0; k < 1 + int(r % 4); ++k) {
                r ^= r << 13; r ^= r >> 7; r ^= r << 17;
                const std::size_t at = std::size_t(r % m.size());
                const char repl[] = "0123456789.-e#()<>/\"' ,;:MLHVCSQTAZmlhvcsqtaz";
                m[at] = repl[(r >> 20) % (sizeof(repl) - 1)];
            }
            DecodedImage t;
            if (Render(m, t)) CHECK(t.rgba.size() == std::size_t(t.width) * std::size_t(t.height) * 4);
            ++n;
        }
        std::printf("  hostile: %d inputs (truncations: %d rendered, the rest refused)\n", n, ok);
    }

    TEST_MAIN_RESULT();
}
