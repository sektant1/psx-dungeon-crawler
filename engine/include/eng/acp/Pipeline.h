#pragma once

#include <eng/acp/Exporter.h>
#include <eng/content/PackManifest.h>
#include <eng/content/ResourceDb.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// The grey box: the Asset Conditioning Pipeline itself.
//
// Four phases, and the value is in the second one:
//
//   scan       walk the content root, classify, load the resource database
//   plan       compute each asset's build key and compare it with the manifest
//              the last run wrote; anything equal is already built
//   export     run the stale ones through their exporter, in parallel
//   publish    write pack.manifest, and delete outputs whose source is gone
//
// The build key is hash(source bytes, import settings, exporter version,
// dependency bytes). Deliberately not timestamps: a git checkout rewrites every
// mtime in the tree, and a pipeline that rebuilds everything after a branch
// switch is one nobody runs, which means content ships unconditioned.
//
// The previous run's manifest IS the build cache. A separate cache file would
// be a second source of truth about what is current, and the two would disagree
// the first time someone deleted one of them.
namespace eng::acp {

struct PipelineOptions {
    std::filesystem::path contentRoot;
    std::filesystem::path outputRoot;

    // Rebuild regardless of the build key. For proving a change to an exporter
    // rather than for daily use.
    bool force = false;
    // Write a .meta for every asset that lacks one. Off by default: the
    // pipeline must be runnable on a clean checkout without dirtying it.
    bool stamp = false;
    // Report what would be built, touch nothing.
    bool dryRun = false;
    // Fail the run if any asset is stale. What CI asks, so a branch cannot
    // merge with content that was never conditioned.
    bool checkOnly = false;
    // 0 means hardware_concurrency.
    unsigned jobs = 0;
    // Empty means every type.
    std::vector<AssetType> only;
    // Substring match on the logical path, for "just rebuild the wands".
    std::string filter;
};

struct AssetOutcome {
    enum class State : uint8_t { UpToDate, Built, Failed, NoExporter, Skipped };
    State state = State::Skipped;
    std::string logical;
    std::string output;
    AssetType type = AssetType::Unknown;
    std::string message;
    std::vector<std::string> warnings;
    double seconds = 0.0;
    uint64_t outputBytes = 0;
};

struct PipelineReport {
    size_t scanned = 0;
    size_t built = 0;
    size_t upToDate = 0;
    size_t failed = 0;
    size_t noExporter = 0;
    size_t skipped = 0;
    size_t removed = 0;
    double seconds = 0.0;
    std::vector<AssetOutcome> outcomes;
    std::vector<content::DbIssue> databaseIssues;

    bool succeeded() const { return failed == 0; }
};

// Called as each asset finishes, from the worker thread that finished it.
// Serialised by the pipeline, so an implementation may print without locking.
using ProgressFn = std::function<void(const AssetOutcome&, size_t done, size_t total)>;

bool run(const PipelineOptions&, const ExporterRegistry&, PipelineReport& report,
         const ProgressFn& progress = {});

// The key an asset would have to have to be considered current. Exposed so the
// editor can show "stale"/"current" beside a record without running a build.
content::Hash buildKey(const Record&, const Exporter&,
                       const std::vector<content::Hash>& dependencyHashes);

} // namespace eng::acp
