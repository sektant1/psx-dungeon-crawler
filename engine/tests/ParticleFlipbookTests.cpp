// The flipbook window: the one piece of maths shared by the material builder
// (which pushes it to the GPU) and particle_sprite.vert (which walks it). It is
// Ogre-free on purpose, so this test needs no renderer.
//
// It also asserts the *contract* between the three files that have to agree
// about the uniform names, because the failure mode when they drift is a
// particle that draws its entire sheet on one quad -- which looks like art, not
// like a bug, and shipped that way once already.

#include <eng/particles/ParticleTypes.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ParticleFlipbookTests: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(float a, float b, const char* message)
{
    require(std::fabs(a - b) < 1e-5f, message);
}

std::string read(const std::string& relativePath)
{
    std::ifstream file(std::string(PROJECT_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

void requireText(const std::string& text, const char* expected,
                 const char* message)
{
    require(text.find(expected) != std::string::npos, message);
}

// What the vertex program computes, transcribed. Keeping it here rather than
// asserting on the uniforms alone is what makes a change to the UV convention
// fail a test instead of a screenshot.
void frameUv(const eng::FlipbookDesc& fb, int frame, float cornerU,
             float cornerV, float& u, float& v)
{
    const int perRow = fb.framesPerRow();
    const int col = frame % perRow;
    const int row = frame / perRow;
    u = fb.originU() + (cornerU + float(col)) * fb.cellU();
    v = fb.originV() + (cornerV + float(row)) * fb.cellV();
}

} // namespace

int main()
{
    // --- defaults: a texture with no flipbook is one full-frame sprite ------
    {
        eng::FlipbookDesc fb;
        require(!fb.active(), "a default flipbook must not animate");
        requireNear(fb.cellU(), 1.0f, "default cell must span the texture");
        requireNear(fb.cellV(), 1.0f, "default cell must span the texture");
        float u = 0, v = 0;
        frameUv(fb, 0, 1.0f, 1.0f, u, v);
        requireNear(u, 1.0f, "default UV must be the whole texture");
        requireNear(v, 1.0f, "default UV must be the whole texture");
    }

    // --- the whole-sheet grid form (`rows`/`cols` in TOML) ------------------
    {
        eng::FlipbookDesc fb;
        fb.sheetCols = 4;
        fb.sheetRows = 4;
        fb.frames = 16;
        fb.perRow = 4;
        fb.fps = 16.0f;
        require(fb.active(), "a 16-frame sheet at 16 fps must animate");
        require(fb.frameCount() == 16, "frame count must be the frame run");

        float u = 0, v = 0;
        frameUv(fb, 0, 0.0f, 0.0f, u, v);
        requireNear(u, 0.0f, "frame 0 starts at the texture origin");
        requireNear(v, 0.0f, "frame 0 starts at the texture origin");
        // Frame 5 is column 1 of row 1 on a 4x4 grid.
        frameUv(fb, 5, 0.0f, 0.0f, u, v);
        requireNear(u, 0.25f, "frame 5 is column 1");
        requireNear(v, 0.25f, "frame 5 is row 1");
        frameUv(fb, 5, 1.0f, 1.0f, u, v);
        requireNear(u, 0.5f, "frame 5 ends one cell later");
        requireNear(v, 0.5f, "frame 5 ends one cell later");
    }

    // --- a strip carved out of a shared sheet -------------------------------
    // This is the case the whole feature exists for: one PNG, one animation per
    // row, and an entry that names a row rather than a file.
    {
        eng::FlipbookDesc fb;
        fb.sheetCols = 8;
        fb.sheetRows = 25;
        fb.originCol = 0;
        fb.originRow = 18;
        fb.frames = 8;
        fb.perRow = 8;
        fb.fps = 14.0f;

        float u = 0, v = 0;
        frameUv(fb, 0, 0.0f, 0.0f, u, v);
        requireNear(u, 0.0f, "the strip starts at column 0");
        requireNear(v, 18.0f / 25.0f, "the strip starts at its own row");
        // The last frame must stay inside the row: a strip that wrapped would
        // animate into the neighbouring effect's art.
        frameUv(fb, 7, 1.0f, 1.0f, u, v);
        requireNear(u, 1.0f, "the last frame ends at the sheet's right edge");
        requireNear(v, 19.0f / 25.0f, "the strip never leaves its row");
    }

    // --- a strip that does not start at column 0 ----------------------------
    {
        eng::FlipbookDesc fb;
        fb.sheetCols = 56;
        fb.sheetRows = 16;
        fb.originCol = 48;
        fb.originRow = 3;
        fb.frames = 8;
        fb.perRow = 8;
        fb.fps = 12.0f;
        float u = 0, v = 0;
        frameUv(fb, 0, 0.0f, 0.0f, u, v);
        requireNear(u, 48.0f / 56.0f, "an offset strip starts at its column");
        frameUv(fb, 7, 1.0f, 0.0f, u, v);
        requireNear(u, 1.0f, "eight cells from column 48 of 56 is the edge");
    }

    // --- perRow 0 means "one unwrapped strip" -------------------------------
    {
        eng::FlipbookDesc fb;
        fb.sheetCols = 10;
        fb.sheetRows = 2;
        fb.frames = 10;
        fb.perRow = 0;
        require(fb.framesPerRow() == 10,
                "perRow 0 must fall back to the whole frame run");
    }

    // --- the three files that must agree on the uniform names ---------------
    const std::string vert = read("assets/shaders/particle_sprite.vert");
    const std::string material =
        read("assets/materials/particles.material");
    const std::string builder =
        read("engine/src/particles/ParticleMaterials.cpp");
    for (const char* name :
         {"flipbookCell", "flipbookOrigin", "flipbookPerRow"}) {
        requireText(vert, name, "particle_sprite.vert lost a flipbook uniform");
        requireText(material, name,
                    "particles.material lost a flipbook default");
        requireText(builder, name,
                    "ParticleMaterials stopped pushing a uniform");
    }
    // The old whole-sheet-only uniform must be gone from all three, or a
    // material could keep declaring a constant nothing writes.
    require(vert.find("flipbookGrid") == std::string::npos &&
                material.find("flipbookGrid") == std::string::npos,
            "flipbookGrid survived the move to an origin/cell window");

    // --- the generated import is present and well formed --------------------
    const std::string sheets =
        read("assets/particles/sprite_sheets.toml");
    require(!sheets.empty(),
            "assets/particles/sprite_sheets.toml is missing; run "
            "tools/import_sprite_sheets.py");
    requireText(sheets, "sheet = ", "an imported entry must name its sheet");
    requireText(sheets, "origin_row", "an imported entry must name its row");

    std::cout << "ParticleFlipbookTests: ok\n";
    return 0;
}
