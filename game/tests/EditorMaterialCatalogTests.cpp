// Classifying materials by what they need from a mesh.
//
// The editor offered all 126 shipped materials for any entity, and most of
// those combinations do not render. The ones that matter fail *quietly* -- a
// particle material draws nothing, a bloom pass samples the framebuffer, an
// atlas smears its whole sheet over one face -- so the check has to happen
// before the click, and it has to be read out of the material script rather
// than from a list of names somebody maintains by hand.

#include "MaterialCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorMaterialCatalogTests: " << message << '\n';
        std::exit(1);
    }
}

static const MaterialInfo* find(const std::vector<MaterialInfo>& all,
                                const std::string& name)
{
    for (const MaterialInfo& info : all)
        if (info.name == name)
            return &info;
    return nullptr;
}

static std::string write(const std::filesystem::path& path,
                         const std::string& body)
{
    std::ofstream out(path);
    out << body;
    return path.string();
}

int main(int argc, char** argv)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "psx_material_catalog";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    // --- classification, one case per way a material binds a mesh -----------
    {
        const std::string path = write(root / "mixed.material", R"(
// A comment that says material Fake/NotReal and mentions texture_unit
material Game/Kit/Dungeon
{
    technique { pass {
        vertex_program_ref PSX_VS_Lit { }
        fragment_program_ref PSX_FS_Dungeon { }
        texture_unit { texture Dungeon_Map.png  tex_address_mode clamp }
    } }
}

material Game/Kit/Stone
{
    technique { pass {
        vertex_program_ref PSX_VS_Lit { }
        fragment_program_ref PSX_FS_Dungeon { }
        texture_unit { texture TEX_Wall_03.png  tex_address_mode wrap }
    } }
}

material Game/Vfx/Lava
{
    technique { pass {
        vertex_program_ref PixelVfx/LiquidVS { }
        fragment_program_ref PixelVfx/LavaFS { }
        texture_unit { texture lava.png  tex_address_mode wrap }
    } }
}

material Engine/Particles/SpriteAlpha
{
    technique { pass {
        vertex_program_ref Particles/SpriteVS { }
        fragment_program_ref AtlasParticle_FS { }
    } }
}

material Engine/Psx/BloomBlurH
{
    technique { pass {
        depth_check off
        cull_hardware none
        vertex_program_ref Dither_VS { }
        fragment_program_ref BloomBlur_FS { }
    } }
}

material Editor/PlacementGhost
{
    technique { pass {
        vertex_program_ref Editor_VS_Checkerboard { }
    } }
}

material Game/NoProgram
{
    technique { pass { } }
}
)");
        const std::vector<MaterialInfo> all = parseMaterialScript(path);
        require(all.size() == 7, "every material in the script is found");
        require(find(all, "Fake/NotReal") == nullptr,
                "and a name inside a comment is not one of them");

        require(find(all, "Game/Kit/Dungeon")->klass == MaterialClass::Atlas,
                "a clamped sheet is an atlas -- its UVs index regions");
        require(find(all, "Game/Kit/Dungeon")->clamped, "and it says so");
        require(find(all, "Game/Kit/Stone")->klass == MaterialClass::Surface,
                "the same shader with a wrapping texture tiles, so it goes "
                "anywhere");
        require(find(all, "Game/Vfx/Lava")->klass == MaterialClass::VfxSurface,
                "a PixelVfx program is an animated surface");
        require(find(all, "Engine/Particles/SpriteAlpha")->klass ==
                    MaterialClass::Particle,
                "an instanced vertex program is a particle material");
        require(find(all, "Engine/Psx/BloomBlurH")->klass ==
                    MaterialClass::PostProcess,
                "a compositor pass is post-process");
        require(find(all, "Editor/PlacementGhost")->klass ==
                    MaterialClass::EditorOnly,
                "and the editor's own are its own");
        require(find(all, "Game/NoProgram")->klass == MaterialClass::Unknown,
                "a pass with no program says nothing about what it wants");

        require(find(all, "Game/Kit/Dungeon")->texture == "Dungeon_Map.png",
                "the texture is captured, for the panel to show");
    }

    // --- what is offered on an entity at all --------------------------------
    {
        require(isEntityMaterial(MaterialClass::Surface), "surfaces, yes");
        require(isEntityMaterial(MaterialClass::Atlas), "atlases, yes");
        require(isEntityMaterial(MaterialClass::VfxSurface), "vfx, yes");
        require(!isEntityMaterial(MaterialClass::PostProcess),
                "a bloom pass is never a choice for an entity");
        require(!isEntityMaterial(MaterialClass::Particle),
                "and neither is a particle material");
        require(!isEntityMaterial(MaterialClass::Sprite), "or a sprite one");
        require(isEntityMaterial(MaterialClass::Unknown),
                "but an unclassified material is still offered -- refusing to "
                "show it would hide a material somebody just wrote");
    }

    // --- the combinations that break ----------------------------------------
    {
        for (const MeshKind mesh : {MeshKind::AtlasUv, MeshKind::Tiling,
                                    MeshKind::Generated, MeshKind::Unknown}) {
            require(materialFits(MaterialClass::PostProcess, mesh).fit ==
                        Fit::Broken,
                    "a compositor pass breaks on every mesh");
            require(materialFits(MaterialClass::Particle, mesh).fit ==
                        Fit::Broken,
                    "so does an instanced one -- it draws nothing at all");
            require(materialFits(MaterialClass::Surface, mesh).fit == Fit::Good,
                    "and a wrapping surface is safe on every mesh, which is "
                    "what makes it the answer for restyling a piece");
        }
        require(!materialFits(MaterialClass::Particle, MeshKind::Tiling)
                     .reason.empty(),
                "a broken fit says what will happen");
    }

    // --- the atlas trap, which is the quiet one -----------------------------
    {
        require(materialFits(MaterialClass::Atlas, MeshKind::AtlasUv).fit ==
                    Fit::Good,
                "an atlas on the kit it was authored for is correct");
        const MaterialAdvice bad =
            materialFits(MaterialClass::Atlas, MeshKind::Generated);
        require(bad.fit == Fit::Risky,
                "on a generated quad it renders -- wrongly, which is worse");
        require(bad.reason.find("stretch") != std::string::npos,
                "and the reason names the symptom the author will see");

        require(materialFits(MaterialClass::VfxSurface, MeshKind::Generated)
                        .fit == Fit::Good,
                "a liquid on a generated quad is what it was written for");
        require(materialFits(MaterialClass::VfxSurface, MeshKind::AtlasUv).fit ==
                    Fit::Risky,
                "on kit geometry its flow follows the atlas layout instead");
    }

    // --- a piece's own material says what its UVs are for -------------------
    {
        require(meshKindForMaterial(MaterialClass::Atlas) == MeshKind::AtlasUv,
                "a piece authored against an atlas has atlas UVs");
        require(meshKindForMaterial(MaterialClass::Surface) == MeshKind::Tiling,
                "one authored against a tiling texture has ordinary ones");
        require(meshKindForMaterial(MaterialClass::Particle) == MeshKind::Unknown,
                "and a class that is not a surface implies nothing");
    }

    // --- a directory, sorted, and the degenerate cases ----------------------
    {
        write(root / "b.material",
              "material Zebra { technique { pass { "
              "vertex_program_ref PSX_VS_Lit { } } } }\n");
        write(root / "a.material",
              "material Aardvark { technique { pass { "
              "vertex_program_ref PSX_VS_Lit { } } } }\n");
        write(root / "notes.txt", "material NotAScript { }\n");

        const std::vector<MaterialInfo> all =
            loadMaterialCatalog((root / "").string());
        require(all.size() >= 2, "every .material in the directory is read");
        require(find(all, "NotAScript") == nullptr,
                "and nothing else is");
        for (std::size_t i = 1; i < all.size(); ++i)
            require(all[i - 1].name <= all[i].name,
                    "sorted, so the panel does not reorder between runs");

        require(loadMaterialCatalog((root / "missing").string()).empty(),
                "a missing directory is an empty catalogue, not an error");
        require(parseMaterialScript((root / "nope.material").string()).empty(),
                "and so is a missing script");

        const std::string truncated =
            write(root / "cut.material",
                  "material Half { technique { pass { "
                  "vertex_program_ref PSX_VS_Lit { }\n");
        const std::vector<MaterialInfo> partial =
            parseMaterialScript(truncated);
        require(partial.size() == 1 && partial[0].name == "Half",
                "a script that ends mid-material still yields what it had");
    }

    // --- the shipped scripts ------------------------------------------------
    if (argc > 1) {
        const std::vector<MaterialInfo> all = loadMaterialCatalog(argv[1]);
        require(all.size() > 50,
                "the shipped catalogue parses -- an empty one is a material "
                "panel that silently lost every entry");
        int unknown = 0;
        for (const MaterialInfo& info : all)
            unknown += info.klass == MaterialClass::Unknown ? 1 : 0;
        require(unknown <= all.size() / 4,
                "and most of it classifies: a catalogue that is mostly "
                "'unknown' is a classifier that has stopped working");

        require(find(all, "Engine/Psx/BloomBlurH") != nullptr &&
                    find(all, "Engine/Psx/BloomBlurH")->klass ==
                        MaterialClass::PostProcess,
                "the real bloom pass is recognised as one");
        require(find(all, "Game/Kit/Dungeon") != nullptr &&
                    find(all, "Game/Kit/Dungeon")->klass == MaterialClass::Atlas,
                "and the real kit atlas as an atlas");
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "EditorMaterialCatalogTests: ok\n";
    return 0;
}
