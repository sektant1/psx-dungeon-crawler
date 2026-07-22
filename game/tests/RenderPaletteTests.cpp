#include "RenderPalette.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "RenderPaletteTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void writeFile(const std::string& path, const std::string& contents)
{
    std::ofstream out(path);
    out << contents;
}
} // namespace

int main()
{
    const std::string path = "/tmp/render_palette_tests.toml";
    writeFile(path,
        "[palette.dungeon]\n"
        "fog_density = 0.077\n"
        "col_depth = 15.0\n"
        "sun_colour_srgb = [0.1, 0.2, 0.3]\n");

    // Present keys override; absent keys keep struct defaults.
    RenderPalette p;
    require(loadRenderPalette(path, "dungeon", p), "dungeon table must load");
    require(near(p.fogDensity, 0.077f), "fog_density parsed");
    require(near(p.colDepth, 15.0f), "col_depth parsed");
    require(near(p.sunColourSrgb.x, 0.1f) && near(p.sunColourSrgb.y, 0.2f) &&
                near(p.sunColourSrgb.z, 0.3f),
            "sun_colour_srgb parsed as 3-vector");
    // An unspecified field keeps its default.
    require(near(p.ambientScale, 0.25f), "unspecified field keeps default");
    require(near(p.bloomIntensity, 0.72f), "unspecified bloom keeps default");

    // Missing table returns false and leaves the struct untouched.
    RenderPalette q;
    require(!loadRenderPalette(path, "nonexistent", q),
            "missing table must return false");
    require(near(q.fogDensity, 0.050f), "failed load leaves defaults");

    // Unparseable file returns false.
    writeFile(path, "this is = = not valid toml [[[\n");
    RenderPalette r;
    require(!loadRenderPalette(path, "dungeon", r),
            "unparseable file must return false");
    require(near(r.fogDensity, 0.050f), "parse failure leaves defaults");

    std::cout << "RenderPaletteTests: OK\n";
    return 0;
}
