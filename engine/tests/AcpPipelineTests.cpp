// The resource database and the pipeline's incremental behaviour, on a content
// tree this test builds itself.
//
// Incrementality is the property most likely to rot without anyone noticing: it
// only ever fails by being *too clever* -- skipping something it should have
// rebuilt -- and the symptom is stale data in a build, hours later, with
// nothing pointing at the cause. So the interesting assertions here are all of
// the form "this changed, therefore exactly this rebuilt".

#include <eng/acp/Pipeline.h>
#include <eng/content/DataAsset.h>
#include <eng/content/PackManifest.h>
#include <eng/content/ResourceDb.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace eng;
using namespace eng::content;

static void require(bool c, const std::string& m)
{
    if (!c) {
        std::cerr << "AcpPipelineTests: " << m << '\n';
        std::exit(1);
    }
}

static void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

// A content tree with one of everything the pipeline can reach without Assimp
// or stb: the rows that need a real DCC file are covered by cooking the actual
// game tree, which is what the acp_content ctest does.
static void buildTree(const fs::path& root)
{
    write(root / "assets.toml", "schema = 1\n[[pack]]\nid = \"content\"\ndir = \".\"\n");
    write(root / "materials" / "demo.mat", "material Demo/One {}\n");
    write(root / "config" / "enemies.toml", "[[enemy]]\nid = \"ghoul\"\n");
    write(root / "config" / "kit.toml",
          "[kit]\nscale = 0.2\nmesh_dir = \"meshes/kit\"\n"
          "[[piece]]\nid = \"kit.wall\"\nmesh = \"Wall.obj\"\n");
    write(root / "meshes" / "kit" / "Wall.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    write(root / "scripts" / "door.lua", "function on_use() end\n");
    write(root / "audio" / "step.wav", "RIFF....WAVEfmt ");
    // A checked-in build artifact beside the source that produces it -- the
    // shape `assets/scenes/*.map` has in the real tree, where a cooked .map was
    // committed next to its .scn.
    write(root / "config" / "kit.rtpl", "a stale committed export\n");
    write(root / "audio" / "README.md", "not an asset\n");
}

static const acp::AssetOutcome* outcomeFor(const acp::PipelineReport& report,
                                      const std::string& logical)
{
    for (const acp::AssetOutcome& outcome : report.outcomes)
        if (outcome.logical == logical)
            return &outcome;
    return nullptr;
}

static size_t builtCount(const acp::PipelineReport& report)
{
    return report.built;
}

static void testResourceDb(const fs::path& root)
{
    ResourceDb db;
    require(db.scan(root), "scan succeeds");
    require(!db.records().empty(), "records found");
    require(db.issues().empty(), "a clean tree has no issues");

    const Record* material = db.findLogical("materials/demo.mat");
    require(material != nullptr, "material found by logical path");
    require(material->type == AssetType::Material, "material classified");
    require(!material->hasSidecar, "no sidecar until stamped");
    require(material->guid != 0, "a guid is assigned without a sidecar");
    require(material->sourceHash != 0, "source hashed");
    require(db.find(material->guid) == material, "found by guid");
    require(db.findLogical("audio/README.md") == nullptr, "docs are skipped");

    // The guid a path gets before it is stamped is derived from the path, so
    // two people adding the same asset independently agree.
    require(material->guid == ResourceDb::guidForLogical("materials/demo.mat"),
            "unstamped guid is derived from the logical path");

    // Stamping writes sidecars; a rescan must then find them and keep the ids.
    ResourceDb stamped;
    ResourceDb::ScanOptions options;
    options.stampMissing = true;
    require(stamped.scan(root, options), "stamping scan succeeds");
    require(fs::exists(root / "materials" / "demo.mat.meta"), "sidecar written");

    ResourceDb reread;
    require(reread.scan(root), "rescan succeeds");
    const Record* again = reread.findLogical("materials/demo.mat");
    require(again != nullptr && again->hasSidecar, "sidecar found on rescan");
    require(again->guid == material->guid, "guid survives a round-trip");
    require(reread.records().size() == db.records().size(),
            "a .meta is not itself an asset");

    // A guid must survive a rename -- that is the whole reason it is stored
    // rather than derived every time.
    const Hash before = again->guid;
    fs::rename(root / "materials" / "demo.mat", root / "materials" / "renamed.mat");
    fs::rename(root / "materials" / "demo.mat.meta",
               root / "materials" / "renamed.mat.meta");
    ResourceDb renamed;
    require(renamed.scan(root), "scan after rename");
    const Record* moved = renamed.findLogical("materials/renamed.mat");
    require(moved != nullptr, "renamed asset found");
    require(moved->guid == before, "guid survives the rename");
    fs::rename(root / "materials" / "renamed.mat", root / "materials" / "demo.mat");
    fs::rename(root / "materials" / "renamed.mat.meta",
               root / "materials" / "demo.mat.meta");
}

static void testSettings()
{
    Settings a;
    a["pivot"] = Setting::fromString("source");
    a["scale"] = Setting::fromNumber(0.2);
    Settings b = a;
    require(hashSettings(a) == hashSettings(b), "equal settings hash equal");
    b["scale"] = Setting::fromNumber(0.2000001);
    require(hashSettings(a) != hashSettings(b), "a changed value changes the key");
    // Kind matters: 1 and 1.0 are different authored intents and must not
    // collide, or a .meta edit would fail to trigger a rebuild.
    Settings c, d;
    c["n"] = Setting::fromInteger(1);
    d["n"] = Setting::fromNumber(1.0);
    require(hashSettings(c) != hashSettings(d), "integer and float differ");
}

static void testPipeline(const fs::path& root, const fs::path& out)
{
    acp::ExporterRegistry registry;
    acp::registerBuiltinExporters(registry);

    acp::PipelineOptions options;
    options.contentRoot = root;
    options.outputRoot = out;
    options.jobs = 2;

    acp::PipelineReport first;
    require(acp::run(options, registry, first), "first build succeeds");
    require(first.built > 0, "first build does work");
    require(first.upToDate == 0, "nothing is up to date on a cold build");
    require(fs::exists(out / kPackManifestName), "manifest written");
    require(fs::exists(out / "materials" / "demo.mat"), "material published");
    require(fs::exists(out / "config" / "kit.rtpl"), "kit exported to rtpl");
    require(fs::exists(out / "scripts" / "door.lua"), "script published");

    // Warm: nothing changed, nothing rebuilds.
    acp::PipelineReport second;
    require(acp::run(options, registry, second), "second build succeeds");
    require(second.built == 0, "a warm build rebuilds nothing");
    require(second.upToDate == first.built, "everything is up to date");

    // --check is what CI runs: green when current.
    acp::PipelineOptions check = options;
    check.checkOnly = true;
    acp::PipelineReport checked;
    require(acp::run(check, registry, checked), "--check passes when current");

    // One file changes: exactly one asset rebuilds.
    write(root / "config" / "enemies.toml", "[[enemy]]\nid = \"ghoul\"\nhp = 12\n");
    acp::PipelineReport third;
    require(acp::run(options, registry, third), "third build succeeds");
    require(builtCount(third) == 1, "exactly one asset rebuilt");
    require(outcomeFor(third, "config/enemies.toml")->state ==
                acp::AssetOutcome::State::Built,
            "and it is the one that changed");

    // ...and --check now fails, before anyone ships the stale pack.
    write(root / "config" / "enemies.toml", "[[enemy]]\nid = \"ghoul\"\nhp = 13\n");
    acp::PipelineReport staleCheck;
    require(!acp::run(check, registry, staleCheck), "--check fails when stale");
    require(staleCheck.failed == 1, "and names the stale asset");
    require(acp::run(options, registry, third), "rebuild after the check");

    // A DEPENDENCY changes. kit.rtpl references meshes/kit/Wall.obj through
    // `mesh_dir`; editing the mesh must rebuild the template even though the
    // template's own bytes did not move. This is the case an incremental build
    // gets wrong by default, and the one that ships stale data.
    PackManifest manifest;
    std::string error;
    require(manifest.load(out, error), error);
    const PackEntry* kit = manifest.bySource("config/kit.toml");
    require(kit != nullptr, "kit is in the manifest");
    require(!kit->dependencies.empty(), "kit recorded a dependency");
    require(kit->dependencies[0] == "meshes/kit/Wall.obj",
            "and it is the mesh named through mesh_dir");

    write(root / "meshes" / "kit" / "Wall.obj",
          "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n");
    acp::PipelineReport fourth;
    require(acp::run(options, registry, fourth), "dependency build succeeds");
    require(outcomeFor(fourth, "config/kit.toml")->state ==
                acp::AssetOutcome::State::Built,
            "a changed dependency rebuilds the asset that named it");

    // A deleted source takes its output with it. Left behind, a deleted asset
    // keeps working for anyone running off the pack.
    fs::remove(root / "scripts" / "door.lua");
    acp::PipelineReport fifth;
    require(acp::run(options, registry, fifth), "build after deletion");
    require(fifth.removed == 1, "one stale output removed");
    require(!fs::exists(out / "scripts" / "door.lua"), "and it is gone");

    // --force ignores the build key entirely.
    acp::PipelineOptions forced = options;
    forced.force = true;
    acp::PipelineReport sixth;
    require(acp::run(forced, registry, sixth), "forced build succeeds");
    require(sixth.built > 0 && sixth.upToDate == 0, "--force rebuilds all");

    // --- a checked-in intermediate must not be published over the output its
    // own source produces. Both records claim `config/kit.rtpl`; before this,
    // both were jobs writing that path from different threads, an edit to the
    // stale committed copy silently replaced the freshly exported one, and
    // --check called the pack current because each build key matched its own
    // source.
    {
        acp::PipelineReport report;
        require(acp::run(options, registry, report), "build with a committed artifact");
        const acp::AssetOutcome* artifact = outcomeFor(report, "config/kit.rtpl");
        require(artifact != nullptr, "the committed .rtpl is reported");
        require(artifact->state == acp::AssetOutcome::State::Skipped,
                "and it is skipped, not published");
        require(artifact->message.find("build artifact") != std::string::npos,
                "with a message saying why");
        // The proof is in the bytes: the pack holds what the exporter wrote,
        // not the text someone committed.
        require(isDataAsset(out / "config" / "kit.rtpl"),
                "the pack holds the exported form, not the committed file");
    }

    // --- a PARTIAL run must not speak for the assets it did not look at.
    // Before this, `--filter` published a manifest holding only the filtered
    // assets and deleted every other file in the pack.
    {
        PackManifest before;
        std::string error;
        require(before.load(out, error), error);
        const size_t fullEntries = before.entries().size();
        size_t fullFiles = 0;
        for (const auto& entry : fs::recursive_directory_iterator(out))
            fullFiles += entry.is_regular_file() ? 1 : 0;

        acp::PipelineOptions filtered = options;
        filtered.filter = "enemies";
        acp::PipelineReport report;
        require(acp::run(filtered, registry, report), "filtered run succeeds");
        require(report.removed == 0, "a partial run removes nothing");

        PackManifest after;
        require(after.load(out, error), error);
        require(after.entries().size() == fullEntries,
                "a filtered run keeps every manifest entry");
        size_t filesAfter = 0;
        for (const auto& entry : fs::recursive_directory_iterator(out))
            filesAfter += entry.is_regular_file() ? 1 : 0;
        require(filesAfter == fullFiles, "and every file in the pack");

        acp::PipelineOptions typed = options;
        typed.only = {AssetType::Material};
        require(acp::run(typed, registry, report), "typed run succeeds");
        require(report.removed == 0, "a typed run removes nothing either");
        PackManifest afterTyped;
        require(afterTyped.load(out, error), error);
        require(afterTyped.entries().size() == fullEntries,
                "a --type run keeps every manifest entry");
    }

    // A record can opt out, which is how a skinned .glb stays out of the static
    // mesh row.
    write(root / "materials" / "demo.mat.meta",
          "schema = 1\ntype = \"material\"\n\n[import]\nskip = true\n");
    acp::PipelineReport seventh;
    require(acp::run(options, registry, seventh), "build with a skip");
    // Two skips now: this one, and the committed build artifact above. Assert
    // on the outcome rather than the count -- a count is a test that breaks
    // every time the fixture grows.
    require(outcomeFor(seventh, "materials/demo.mat")->state ==
                acp::AssetOutcome::State::Skipped,
            "the record that asked to opt out did");
    require(seventh.skipped >= 1, "and the summary counts it");
}

int main()
{
    const fs::path base = fs::temp_directory_path() / "raven_acp_tests";
    fs::remove_all(base);
    const fs::path root = base / "assets";
    const fs::path out = base / "cooked";
    buildTree(root);

    testResourceDb(root);
    testSettings();
    testPipeline(root, out);

    fs::remove_all(base);
    std::cout << "AcpPipelineTests OK\n";
    return 0;
}
