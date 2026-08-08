// The model import pipeline: a multi-submesh OBJ with a sidecar .mtl and its
// textures, imported into a scratch pack.
//
// The fixture is WRITTEN BY THIS TEST, and that is the point. It used to be a
// real model that happened to be in the tree (`assets/source/models/mecha-dl`),
// on the reasoning that a real file exercises the importer more honestly than a
// synthetic one. That reasoning was wrong in a specific way: the model was
// prototype content, it was deleted when the project's direction changed, and
// the test then failed for a reason that had nothing to do with the importer.
//
// A test of the importer should depend on the importer. So the fixture below is
// the smallest thing that covers every branch this file asserts on -- two
// submeshes with different materials, a .mtl naming textures by a path relative
// to the model, and PNGs to find at the end of those paths.
//
// Everything is written into a scratch asset root, because a successful import
// rewrites kit.toml and a test that edits the shipped catalogue is a test that
// breaks the game.

#include <editor/assets/ModelImportPipeline.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorModelImportTests: " << message << '\n';
        std::exit(1);
    }
}

static std::string readAll(const fs::path& path)
{
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// A 1x1 PNG, encoded by hand. Small enough to sit in the source, real enough
// that the importer's "is this a readable image" path is the same one it runs
// on shipped art.
static void writeTinyPng(const fs::path& path)
{
    static const unsigned char kPng[] = {
        0x89, 'P',  'N',  'G',  0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        'I',  'H',  'D',  'R',  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0C, 'I',  'D',  'A',  'T',  0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0xB0, 0x00, 0x00, 0x00,
        0x00, 'I',  'E',  'N',  'D',  0xAE, 0x42, 0x60, 0x82,
    };
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
}

// Two submeshes, two materials, two textures named relative to the model.
// Resolving those against the MODEL's directory rather than the working
// directory is the whole reason ModelImportPipeline::resolveTexture exists, so
// the fixture has to be somewhere that is not the working directory.
static fs::path writeFixture(const fs::path& directory)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    writeTinyPng(directory / "386.png");
    writeTinyPng(directory / "387.png");

    {
        std::ofstream mtl(directory / "mechademonlord.mtl");
        mtl << "newmtl hull\nKd 0.8 0.8 0.8\nmap_Kd 386.png\n\n"
               "newmtl trim\nKd 0.6 0.6 0.6\nmap_Kd 387.png\n";
    }

    const fs::path model = directory / "mechademonlord.obj";
    std::ofstream obj(model);
    obj << "mtllib mechademonlord.mtl\n";
    // Two disjoint triangles, each with its own material, so the importer sees
    // a genuinely multi-part model and the group/attachment logic is exercised.
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    obj << "v 4 0 0\nv 5 0 0\nv 4 1 0\n";
    obj << "vt 0 0\nvt 1 0\nvt 0 1\n";
    obj << "vn 0 0 1\n";
    obj << "usemtl hull\no hull\nf 1/1/1 2/2/1 3/3/1\n";
    obj << "usemtl trim\no trim\nf 4/1/1 5/2/1 6/3/1\n";
    return model;
}

int main()
{
    std::error_code ec;
    const fs::path root =
        fs::temp_directory_path() / "raven_model_import_tests";
    fs::remove_all(root, ec);
    fs::create_directories(root / "config", ec);
    require(!ec, "scratch asset root was created");

    const fs::path model = writeFixture(root / "source" / "mecha-dl");
    require(fs::is_regular_file(model), "the fixture model was written");
    {
        std::ofstream kit(root / "config" / "kit.toml");
        kit << "scale = 0.2\ncell_size = 20\nmesh_dir = \"meshes\"\n";
    }

    require(modelSlug(model.string()) == "mechademonlord",
            "the id stem comes from the file name, lowercased");

    const ModelImportResult result = importModelToKit(model.string(), root.string());
    require(result.ok, "the OBJ imports: " + result.error);
    require(!result.parts.empty(), "and yields at least one placeable part");

    // --- geometry ----------------------------------------------------------
    for (const ImportedPart& part : result.parts) {
        require(part.prefab.rfind("kit.import_mechademonlord", 0) == 0,
                "every part is prefixed with the model's slug: " + part.prefab);
        const std::string stem = part.prefab.substr(4); // drop "kit."
        const fs::path obj = root / "meshes" / "props" / (stem + ".obj");
        require(fs::is_regular_file(obj), "an OBJ was written for " + stem);
        const std::string text = readAll(obj);
        require(text.find("\nv ") != std::string::npos, "with vertices");
        require(text.find("\nvn ") != std::string::npos, "with normals");
        require(text.find("\nvt ") != std::string::npos, "with texcoords");
        require(text.find("\nf ") != std::string::npos, "with faces");
    }

    // --- textures ----------------------------------------------------------
    //
    // The .mtl names 386.png and 387.png relative to the model. Resolving those
    // against the model's own directory -- not the working directory -- is the
    // whole reason resolveTexture exists.
    require(!result.textures.empty(),
            "the sidecar .mtl's textures were found and copied");
    for (const std::string& texture : result.textures) {
        require(fs::is_regular_file(root / "textures" / texture),
                "texture landed in the pack: " + texture);
        require(texture.rfind("import_mechademonlord_", 0) == 0,
                "and is namespaced, so two models may both ship a 386.png: " +
                    texture);
    }

    require(!result.materialScript.empty(),
            "a material script was written for the textured parts");
    const std::string material = readAll(result.materialScript);
    require(material.find("filter = \"nearest\"") != std::string::npos,
            "using nearest filtering -- a bilinear 64x64 PSX texture is "
            "exactly the wrong look");
    require(material.find("shader = \"lit\"") != std::string::npos,
            "and the project's own lit shader");

    // --- catalogue ---------------------------------------------------------
    const std::string kit = readAll(root / "config" / "kit.toml");
    require(kit.find("# BEGIN editor import: mechademonlord") != std::string::npos,
            "the kit block is marked");
    require(kit.find("# END editor import: mechademonlord") != std::string::npos,
            "at both ends");
    require(kit.find("socket = \"prop\"") != std::string::npos,
            "imported pieces are free-standing props");
    require(kit.find("scale = 0.2") != std::string::npos,
            "and the file's existing content survived the rewrite");

    // --- the model itself is a placeable -----------------------------------
    //
    // mecha-dl arrives as two submeshes, so the parts alone would leave the
    // Placeables list holding two halves of a mech and no mech. The group is
    // what an author places; its attachments are what reassemble it, and the
    // editor turns those into child entities so the halves stay reachable.
    require(result.root == "kit.import_mechademonlord",
            "a multi-part model reports the group as its root: " + result.root);
    require(kit.find("id = \"import_mechademonlord\"\n") != std::string::npos,
            "and the group is in the catalogue");
    require(kit.find("attachments = [") != std::string::npos,
            "declaring its parts");
    for (const ImportedPart& part : result.parts) {
        require(kit.find("prefab = \"" + part.prefab + "\"") != std::string::npos,
                "every part is attached to the group: " + part.prefab);
        require(part.prefab != result.root,
                "and the group is not one of its own parts");
    }
    // The group names no mesh: its geometry IS its parts. The piece before it
    // in the block is a part, and every part has one, so counting mesh lines
    // against piece lines is the check that the group has none.
    {
        const std::size_t at = kit.find("id = \"import_mechademonlord\"\n");
        const std::size_t nextPiece = kit.find("[[piece]]", at);
        const std::size_t mesh = kit.find("mesh = ", at);
        require(mesh == std::string::npos || mesh > nextPiece ||
                    nextPiece == std::string::npos,
                "the group declares no mesh of its own");
    }

    // --- reimport replaces, never accumulates ------------------------------
    const ModelImportResult again =
        importModelToKit(model.string(), root.string());
    require(again.ok, "the same model imports a second time: " + again.error);
    require(again.parts.size() == result.parts.size(),
            "yielding the same parts");
    const std::string kitAgain = readAll(root / "config" / "kit.toml");
    std::size_t blocks = 0;
    for (std::size_t at = kitAgain.find("# BEGIN editor import: mechademonlord");
         at != std::string::npos;
         at = kitAgain.find("# BEGIN editor import: mechademonlord", at + 1))
        ++blocks;
    require(blocks == 1,
            "and leaving exactly one block -- reimporting after a fix in "
            "Blender must replace the pieces, not shadow them with a second "
            "dead copy");

    // --- textures filed away from the model ---------------------------------
    //
    // The mecha-dl .mtl names "386.png" beside the .obj. Downloaded packs
    // routinely put the model in one folder and its textures in another, and
    // name them as if they were adjacent, so the resolver walks the model's
    // directory and its parent.
    {
        const fs::path pack = root / "packlike";
        fs::create_directories(pack / "source", ec);
        fs::create_directories(pack / "textures", ec);
        fs::copy_file(model, pack / "source" / "mechademonlord.obj",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(model.parent_path() / "mechademonlord.mtl",
                      pack / "source" / "mechademonlord.mtl",
                      fs::copy_options::overwrite_existing, ec);
        // Only in the sibling folder, deliberately not beside the model.
        fs::copy_file(model.parent_path() / "386.png", pack / "textures" / "386.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(model.parent_path() / "387.png", pack / "textures" / "387.png",
                      fs::copy_options::overwrite_existing, ec);
        require(!ec, "the pack-like layout was staged");

        const ModelImportResult packed = importModelToKit(
            (pack / "source" / "mechademonlord.obj").string(), root.string());
        require(packed.ok, "it imports: " + packed.error);
        require(packed.textures.size() == 2,
                "and both textures are found in the sibling folder rather than "
                "reported missing");
        for (const std::string& warning : packed.warnings)
            require(warning.find("texture not found") == std::string::npos,
                    "with no missing-texture warning: " + warning);
    }

    // --- a texture named in a format that is not the one on disk ------------
    //
    // An .mtl saying map_Kd wall.tga while wall.png is what shipped is one of
    // the most common ways a downloaded model arrives.
    {
        const fs::path swapped = root / "swapped";
        fs::create_directories(swapped / "art", ec);
        fs::copy_file(model, swapped / "mechademonlord.obj",
                      fs::copy_options::overwrite_existing, ec);
        // The .mtl asks for .tga; only .png exists, and one directory down.
        {
            std::ifstream in(model.parent_path() / "mechademonlord.mtl");
            std::ostringstream buffer;
            buffer << in.rdbuf();
            std::string mtl = buffer.str();
            for (std::string name : {std::string("386"), std::string("387")}) {
                const std::string from = "map_Kd " + name + ".png";
                const std::string to = "map_Kd " + name + ".tga";
                const std::size_t at = mtl.find(from);
                if (at != std::string::npos)
                    mtl.replace(at, from.size(), to);
            }
            std::ofstream out(swapped / "mechademonlord.mtl");
            out << mtl;
        }
        fs::copy_file(model.parent_path() / "386.png", swapped / "art" / "386.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(model.parent_path() / "387.png", swapped / "art" / "387.png",
                      fs::copy_options::overwrite_existing, ec);
        require(!ec, "the extension-mismatch layout was staged");

        const ModelImportResult found = importModelToKit(
            (swapped / "mechademonlord.obj").string(), root.string());
        require(found.ok, "it imports: " + found.error);
        require(found.textures.size() == 2,
                "the .png on disk answers for the .tga the material asked for");
        for (const std::string& texture : found.textures)
            require(texture.size() > 4 &&
                        texture.compare(texture.size() - 4, 4, ".png") == 0,
                    "and is copied under its real extension, so the material "
                    "names a file that exists: " + texture);
    }

    // --- only loadable formats are listed -----------------------------------
    {
        const std::vector<std::string>& extensions = textureExtensions();
        const auto lists = [&extensions](const std::string& ext) {
            return std::find(extensions.begin(), extensions.end(), ext) !=
                   extensions.end();
        };
        require(lists(".png") && lists(".tga") && !lists(".dds"),
                "the stb_image formats are searched, and DDS is not: the "
                "renderer lost its DDS decoder along with OGRE");
        // FreeImage is off, so these genuinely cannot be loaded. Copying one
        // would give a material naming a file the renderer chokes on.
        require(!lists(".tif") && !lists(".exr") && !lists(".webp"),
                "and the formats this build cannot read are not");
    }

    // --- refusals ----------------------------------------------------------
    {
        const ModelImportResult missing =
            importModelToKit((root / "no_such_model.obj").string(),
                             root.string());
        require(!missing.ok && missing.error.find("no such file") !=
                                   std::string::npos,
                "a missing file is refused by name");
    }
    {
        const fs::path notAModel = root / "config" / "kit.toml";
        const ModelImportResult wrong =
            importModelToKit(notAModel.string(), root.string());
        require(!wrong.ok, "and so is a file Assimp cannot read");
    }

    fs::remove_all(root, ec);
    std::cout << "EditorModelImportTests: ok\n";
    return 0;
}
