#pragma once

#include <eng/content/AssetType.h>
#include <eng/content/ResourceDb.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The processor boxes of figure 1.33: Mesh Exporter, Compression, Particle
// Exporter, Audio Management Tool, Object Model Editor, World Editor.
//
// One interface for all of them, because the pipeline's job -- decide what is
// stale, run the right converter, record what came out -- is the same whatever
// the row. An exporter is a pure function of (source bytes, import settings):
// given the same two it must produce the same output, or the build key is a
// lie and incremental builds start shipping stale data. That is also what makes
// them safe to run on a thread pool, which is the only reason a full cook of
// this tree finishes in seconds.
//
// Exporters live above the engine, in eng_acp. Nothing in eng links this
// header; the game reads what exporters produce (eng/content/*) and never
// learns how it was made.
namespace eng::acp {

using content::AssetType;
using content::Record;

struct ExportContext {
    std::filesystem::path contentRoot; // absolute, holds assets.toml
    std::filesystem::path outputRoot;  // absolute, the cooked pack directory
    const Record* record = nullptr;    // resource database entry, never null
    std::filesystem::path sourcePath;  // absolute, the file to read
    std::filesystem::path outputPath;  // absolute, the file to write
    std::string outputLogical;         // outputPath relative to outputRoot
};

struct ExportResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;

    // Files this asset also read, content-root-relative. Folded into the build
    // key, so editing a mesh's .mtl or a bank's .wav rebuilds the asset that
    // referenced it. Without this the pipeline is incremental and wrong, which
    // is worse than not being incremental.
    std::vector<std::string> dependencies;

    // Outputs beyond outputPath, relative to the output root. The world
    // exporter is the one that uses it today; a mesh exporter that split a
    // skeleton out would too.
    std::vector<std::string> extraOutputs;
};

class Exporter {
public:
    virtual ~Exporter() = default;

    // The diagram's label for this box, verbatim, because that is what the
    // pipeline prints and what the documentation has to match.
    virtual std::string_view name() const = 0;
    virtual AssetType type() const = 0;

    // Bumped whenever the output bytes for an unchanged input would change.
    // It is part of every build key, so bumping it rebuilds exactly the assets
    // this exporter owns and nothing else.
    virtual uint32_t version() const = 0;

    // Where this record's output goes, content-root-relative. The default is
    // the type's intermediate extension from the format table, which is what
    // every row wants except one: the Compression row publishes a `.rtex` only
    // when an asset actually asked to be compressed, and passes the source
    // image through untouched otherwise. Asking the exporter rather than the
    // table is what lets that be a per-asset decision instead of a per-type one.
    virtual std::string outputFor(const Record&) const;

    virtual ExportResult run(const ExportContext&) const = 0;
};

// Which exporter owns a type. Built-ins are registered by
// registerBuiltinExporters(); the World row is registered by the CLI, because
// the .scn cooker lives in game_content and the engine must not link it.
class ExporterRegistry {
public:
    ExporterRegistry();

    void add(std::unique_ptr<Exporter> exporter);
    const Exporter* find(AssetType type) const;
    std::vector<const Exporter*> all() const;

    // The row for an asset that is ALREADY an intermediate: a `.map` checked in
    // beside its `.scn`, an `.ozz` clip, a `.rmesh` someone committed. It goes
    // into the pack unchanged. Running the type's exporter on it instead would
    // ask the World Editor row to parse a cooked map as a scene, which is what
    // it did before this existed.
    const Exporter& publisher() const { return *mPublisher; }

private:
    std::vector<std::unique_ptr<Exporter>> mExporters;
    std::unique_ptr<Exporter> mPublisher;
};

// Mesh, Compression, Particle, Audio Management, Object Model, Animation Tree,
// and the pass-through rows the diagram routes straight to the pipeline.
void registerBuiltinExporters(ExporterRegistry&);

// Converts a mesh record's import settings into the engine's own options
// struct. Exposed because the editor shows the same values and must resolve
// them the same way.
struct ModelImportSettings {
    int pivot = 0;          // eng::PivotMode
    int texcoordV = 0;      // eng::TexcoordVMode
    float metresPerSourceUnit = 1.0f;
    bool generateCollision = true;
};
ModelImportSettings meshImportSettings(const content::Settings&);

} // namespace eng::acp
