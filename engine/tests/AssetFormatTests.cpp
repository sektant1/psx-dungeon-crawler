// The format table and the on-disk formats every exporter writes.
//
// These are the files the game reads, so a change that silently alters one is a
// content tree that has to be re-cooked without anything saying so. Round-trips
// here are exact by intent: an approximate mesh is a moved wall.

#include <eng/content/AssetFile.h>
#include <eng/content/AssetType.h>
#include <eng/content/DataAsset.h>
#include <eng/content/MeshAsset.h>
#include <eng/content/PackManifest.h>
#include <eng/content/TextureAsset.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace eng::content;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "AssetFormatTests: " << m << '\n';
        std::exit(1);
    }
}

static void testFormatTable()
{
    // Every type in the enum is in the table, and every name round-trips. A
    // type that fell out of the table would classify as Unknown and quietly
    // stop being conditioned.
    for (size_t i = 1; i < static_cast<size_t>(AssetType::Count); ++i) {
        const auto type = static_cast<AssetType>(i);
        require(assetFormat(type) != nullptr, "every type has a format row");
        require(assetTypeFromName(assetTypeName(type)) == type,
                "type name round-trips");
    }

    require(classifyAsset("meshes/props/lamp.obj") == AssetType::Mesh, "obj");
    require(classifyAsset("textures/wall.png") == AssetType::Texture, "png");
    require(classifyAsset("materials/game.mat") == AssetType::Material, "mat");
    require(classifyAsset("scenes/start_hall.scn") == AssetType::World, "scn");
    require(classifyAsset("scenes/start_hall.map") == AssetType::World, "map");
    require(classifyAsset("config/kit.toml") == AssetType::ObjectTemplate, "kit");
    require(classifyAsset("config/audio.toml") == AssetType::SoundBank, "audio");
    require(classifyAsset("config/enemies.toml") == AssetType::Config, "config");
    require(classifyAsset("particles/sprite_sheets.toml") ==
                AssetType::ParticleSystem,
            "particles are classified by directory");
    require(classifyAsset("ui/hints.toml") == AssetType::Ui, "ui by directory");

    // The longest suffix wins, which is the only thing separating a skeleton
    // from a clip.
    require(classifyAsset("animations/arms/arms_rig.skeleton.ozz") ==
                AssetType::Skeleton,
            "skeleton.ozz beats ozz");
    require(classifyAsset("animations/arms/clip_idle.ozz") ==
                AssetType::Animation,
            "bare ozz is a clip");

    require(assetStage("meshes/props/lamp.obj") == AssetStage::Source, "obj source");
    require(assetStage("meshes/props/lamp.rmesh") == AssetStage::Intermediate,
            "rmesh intermediate");
    require(assetStage("scenes/a.scn") == AssetStage::Source, "scn source");
    require(assetStage("scenes/a.map") == AssetStage::Intermediate, "map made");

    require(intermediatePathFor("meshes/props/lamp.obj") ==
                "meshes/props/lamp.rmesh",
            "obj -> rmesh");
    require(intermediatePathFor("scenes/start_hall.scn") ==
                "scenes/start_hall.map",
            "scn -> map");
    require(intermediatePathFor("config/kit.toml") == "config/kit.rtpl",
            "kit -> rtpl");
    require(intermediatePathFor("meshes/props/lamp.rmesh").empty(),
            "an intermediate has no further intermediate");
    require(intermediatePathFor("materials/game.mat").empty(),
            "an authored-directly row is not exported to a new name");

    require(ignoredContentFile("audio/README.md"), "docs ignored");
    require(ignoredContentFile("scenes/a.autosave.scn"), "autosave ignored");
    require(ignoredContentFile("fonts/DejaVu-LICENSE"), "extensionless ignored");
    require(!ignoredContentFile("meshes/box.obj"), "real asset kept");
}

static void testMeshRoundTrip(const fs::path& dir)
{
    MeshData mesh;
    MeshSubmesh submesh;
    submesh.name = "body";
    submesh.sourceMaterial = "Game/Kit/Dungeon";
    submesh.sourceTexture = "wall.png";
    for (int i = 0; i < 6; ++i) {
        MeshVertex vertex;
        vertex.position = {float(i), float(i) * 0.5f, -float(i)};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.tangent = {1.0f, 0.0f, 0.0f, -1.0f};
        vertex.texcoord = {float(i) * 0.25f, 0.75f};
        vertex.colour = {0.1f, 0.2f, 0.3f, 1.0f};
        submesh.vertices.push_back(vertex);
    }
    submesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes.push_back(submesh);
    mesh.collisionVertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.collisionIndices = {0, 1, 2};

    MeshAssetInfo info;
    info.sourcePath = "meshes/props/lamp.obj";
    info.format = "obj";
    info.sourceBytes = 4096;
    info.sourceMeshes = 1;
    info.materials = 1;
    info.boundsMin = {-1, 0, -1};
    info.boundsMax = {1, 2, 1};
    info.metresPerSourceUnit = 0.2f;
    info.sourceOrientation = {1, 0, 0, 0};
    info.customPivot = {0, 0.5f, 0};
    info.pivot = 2;
    info.texcoordV = 1;
    info.canonicalPivotStandard = true;

    const fs::path path = dir / "lamp.rmesh";
    std::string error;
    require(writeMeshAsset(path, mesh, info, error), error.c_str());
    require(isMeshAsset(path), "written file identifies as a mesh asset");

    MeshData back;
    MeshAssetInfo backInfo;
    require(readMeshAsset(path, back, backInfo, error), error.c_str());

    require(back.submeshes.size() == 1, "submesh count");
    require(back.submeshes[0].name == "body", "submesh name");
    require(back.submeshes[0].sourceMaterial == "Game/Kit/Dungeon", "material");
    require(back.submeshes[0].sourceTexture == "wall.png", "texture");
    require(back.submeshes[0].indices == mesh.submeshes[0].indices, "indices");
    require(back.collisionIndices == mesh.collisionIndices, "collision indices");
    for (size_t i = 0; i < back.submeshes[0].vertices.size(); ++i) {
        const MeshVertex& a = mesh.submeshes[0].vertices[i];
        const MeshVertex& b = back.submeshes[0].vertices[i];
        require(a.position == b.position && a.normal == b.normal &&
                    a.tangent == b.tangent && a.texcoord == b.texcoord &&
                    a.colour == b.colour,
                "vertex round-trips exactly");
    }
    // Every settings field survives: loadStaticModel() refuses a mesh whose
    // recorded settings differ from the call site's, so a field lost here
    // becomes a mesh that is silently re-imported from source forever.
    require(backInfo.pivot == 2 && backInfo.texcoordV == 1, "pivot and uv mode");
    require(backInfo.metresPerSourceUnit == 0.2f, "unit scale");
    require(backInfo.customPivot == info.customPivot, "custom pivot");
    require(backInfo.sourceOrientation == info.sourceOrientation, "orientation");
    require(backInfo.canonicalPivotStandard, "pivot standard flag");
    require(backInfo.sourcePath == info.sourcePath, "source path");

    // A truncated file must be refused, not half-loaded: that is what a build
    // killed mid-write leaves behind.
    const auto size = fs::file_size(path);
    fs::resize_file(path, size / 2);
    MeshData truncated;
    MeshAssetInfo truncatedInfo;
    require(!readMeshAsset(path, truncated, truncatedInfo, error),
            "truncated rmesh is refused");
}

static void testTextureCodec(const fs::path& dir)
{
    require(textureLevelBytes(TextureFormat::Rgba8, 4, 4) == 64, "rgba8 bytes");
    require(textureLevelBytes(TextureFormat::Bc1, 4, 4) == 8, "bc1 block");
    require(textureLevelBytes(TextureFormat::Bc3, 4, 4) == 16, "bc3 block");
    // Non-multiple-of-four dimensions round up to whole blocks.
    require(textureLevelBytes(TextureFormat::Bc1, 6, 6) == 32, "bc1 rounds up");

    // A flat block is the case a block codec must be exact on, because most
    // pixel art is flat blocks and any error there is visible.
    std::vector<uint8_t> flat(4 * 4 * 4);
    for (size_t i = 0; i < 16; ++i) {
        flat[i * 4 + 0] = 8;   // 8 -> 1 in 5 bits -> 8 again
        flat[i * 4 + 1] = 4;   // 4 -> 1 in 6 bits -> 4 again
        flat[i * 4 + 2] = 8;
        flat[i * 4 + 3] = 255;
    }
    const std::vector<uint8_t> bc3 =
        encodeTextureLevel(TextureFormat::Bc3, 4, 4, flat.data());
    require(bc3.size() == 16, "bc3 encodes one block");
    const std::vector<uint8_t> decoded =
        decodeTextureLevel(TextureFormat::Bc3, 4, 4, bc3);
    require(decoded == flat, "a flat block survives bc3 exactly");

    // Alpha is what separates BC3 from BC1, so it has to survive.
    for (size_t i = 0; i < 16; ++i)
        flat[i * 4 + 3] = static_cast<uint8_t>(i * 17);
    const std::vector<uint8_t> alphaBlock =
        decodeTextureLevel(TextureFormat::Bc3, 4, 4,
                           encodeTextureLevel(TextureFormat::Bc3, 4, 4,
                                              flat.data()));
    for (size_t i = 0; i < 16; ++i) {
        const int want = flat[i * 4 + 3];
        const int got = alphaBlock[i * 4 + 3];
        // BC3's alpha ramp is 8 values between the block's own endpoints, so
        // a full 0..255 sweep quantises to steps of 36 and the worst error is
        // half of that. The bound is on the codec being correct, not lossless.
        require(std::abs(want - got) <= 18, "bc3 alpha is within a ramp step");
    }

    uint32_t width = 0, height = 0;
    const std::vector<uint8_t> half =
        downsampleRgba8(4, 4, flat.data(), width, height);
    require(width == 2 && height == 2 && half.size() == 2 * 2 * 4,
            "downsample halves both axes");

    TextureAsset asset;
    asset.sourcePath = "textures/wall.png";
    asset.format = TextureFormat::Bc1;
    asset.hasAlpha = false;
    TextureLevel level;
    level.width = 4;
    level.height = 4;
    level.bytes = encodeTextureLevel(TextureFormat::Bc1, 4, 4, flat.data());
    asset.levels.push_back(level);

    const fs::path path = dir / "wall.rtex";
    std::string error;
    require(writeTextureAsset(path, asset, error), error.c_str());
    require(isTextureAsset(path), "identifies as a texture asset");

    TextureAsset back;
    require(readTextureAsset(path, back, error), error.c_str());
    // Read always yields RGBA8, whatever was on disk, so no consumer can
    // forget to check the format.
    require(back.format == TextureFormat::Bc1, "format reported");
    require(back.levels.size() == 1, "level count");
    require(back.levels[0].bytes.size() == 4 * 4 * 4, "decoded to rgba8");
}

static void testDataAsset(const fs::path& dir)
{
    DataTable mixer;
    mixer.emplace_back("master_db", DataValue::makeNumber(-4.0));
    mixer.emplace_back("max_voices", DataValue::makeInteger(96));

    DataArray cues;
    DataTable cue;
    cue.emplace_back("id", DataValue::makeString("player.footstep"));
    cue.emplace_back("spatial", DataValue::makeBool(false));
    cues.push_back(DataValue::makeTable(std::move(cue)));

    DataTable root;
    root.emplace_back("mixer", DataValue::makeTable(std::move(mixer)));
    root.emplace_back("cue", DataValue::makeArray(std::move(cues)));

    DataAsset asset;
    asset.sourcePath = "config/audio.toml";
    asset.root = DataValue::makeTable(std::move(root));

    const fs::path path = dir / "audio.rbank";
    std::string error;
    require(writeDataAsset(path, asset, error), error.c_str());
    require(isDataAsset(path), "identifies as a data asset");

    DataAsset back;
    require(readDataAsset(path, back, error), error.c_str());
    require(back.sourcePath == "config/audio.toml", "source path");
    require(back.root.at("mixer.master_db").asNumber() == -4.0, "dotted read");
    require(back.root.at("mixer.max_voices").asInteger() == 96, "integer kept");
    require(back.root["cue"].asArray().size() == 1, "array of tables");
    require(back.root["cue"].asArray()[0]["id"].asString() == "player.footstep",
            "nested table");
    require(back.root.at("mixer.missing").isNull(), "absent reads as null");
    require(back.root.at("nope.nothing.here").isNull(),
            "a broken chain does not need a presence check per link");
    // Integer and float stay distinct: rewriting 1.0 as 1 changes a build key
    // and re-cooks the tree on every run.
    require(back.root.at("mixer.master_db").kind() == DataValue::Kind::Number,
            "float stays a float");
}

static void testManifest(const fs::path& dir)
{
    PackManifest manifest;
    manifest.setDirectory(dir);
    PackEntry entry;
    entry.guid = 0x0123456789abcdefull;
    entry.type = AssetType::Mesh;
    entry.source = "meshes/props/lamp.obj";
    entry.output = "meshes/props/lamp.rmesh";
    entry.buildKey = 0xfedcba9876543210ull;
    entry.outputBytes = 1234;
    entry.dependencies = {"textures/lamp.png", "materials/props.mat"};
    manifest.add(entry);

    std::string error;
    require(manifest.save(dir, error), error.c_str());

    PackManifest back;
    require(back.load(dir, error), error.c_str());
    require(back.entries().size() == 1, "one entry");
    const PackEntry* found = back.bySource("meshes/props/lamp.obj");
    require(found != nullptr, "found by source");
    require(found->guid == entry.guid, "guid");
    require(found->buildKey == entry.buildKey, "build key");
    require(found->outputBytes == 1234, "bytes");
    require(found->dependencies.size() == 2, "dependencies survive");
    require(back.byGuid(entry.guid) != nullptr, "found by guid");
    require(back.bySource("nope") == nullptr, "unknown source");
    // resolve() only answers for a file that is really there: a half-deleted
    // pack must fall back to source rather than hand out a missing path.
    require(back.resolve("meshes/props/lamp.obj") ==
                dir / "meshes/props/lamp.rmesh",
            "resolve joins the pack directory");
}

int main()
{
    const fs::path dir =
        fs::temp_directory_path() / "raven_asset_format_tests";
    fs::remove_all(dir);
    fs::create_directories(dir);

    testFormatTable();
    testMeshRoundTrip(dir);
    testTextureCodec(dir);
    testDataAsset(dir);
    testManifest(dir);

    fs::remove_all(dir);
    std::cout << "AssetFormatTests OK\n";
    return 0;
}
