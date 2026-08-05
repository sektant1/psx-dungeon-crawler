#include <eng/acp/Pipeline.h>

#include <eng/Log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace eng::acp {
namespace {

using content::Hash;
using content::PackEntry;
using content::PackManifest;

double now()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// One planned unit of work. Built on the main thread so the resource database
// and the previous manifest are never touched concurrently.
struct Job {
    const Record* record = nullptr;
    const Exporter* exporter = nullptr;
    std::string outputLogical;
    Hash key = 0;
    std::vector<std::string> dependencies; // from the previous run
};

// Dependency hashes come from the resource database when the dependency is
// itself a tracked asset, and from the file otherwise -- a .mtl beside an .obj
// is a real dependency and not something the scanner classifies.
std::vector<Hash> dependencyHashes(const content::ResourceDb& db,
                                   const std::vector<std::string>& logicals)
{
    std::vector<Hash> out;
    out.reserve(logicals.size());
    for (const std::string& logical : logicals) {
        if (const Record* record = db.findLogical(logical))
            out.push_back(record->sourceHash);
        else
            out.push_back(content::hashFile(db.root() / fs::path(logical)));
    }
    return out;
}

} // namespace

Hash buildKey(const Record& record, const Exporter& exporter,
              const std::vector<Hash>& dependencies)
{
    Hash key = content::hashText(record.logical);
    key = content::hashValue(record.sourceHash, key);
    key = content::hashValue(content::hashSettings(record.import), key);
    key = content::hashValue(exporter.version(), key);
    key = content::hashValue(static_cast<uint64_t>(exporter.type()), key);
    // Sorted, so a dependency list that arrives in a different order between
    // runs -- and Assimp's material order is not something to rely on -- does
    // not read as a change.
    std::vector<Hash> sorted = dependencies;
    std::sort(sorted.begin(), sorted.end());
    for (Hash dependency : sorted)
        key = content::hashValue(dependency, key);
    return key;
}

bool run(const PipelineOptions& options, const ExporterRegistry& registry,
         PipelineReport& report, const ProgressFn& progress)
{
    const double started = now();
    report = {};

    if (options.contentRoot.empty() || options.outputRoot.empty()) {
        log::error("acp: content root and output root are both required");
        return false;
    }

    // --- scan ---------------------------------------------------------------
    content::ResourceDb db;
    content::ResourceDb::ScanOptions scan;
    scan.stampMissing = options.stamp && !options.dryRun && !options.checkOnly;
    scan.types = options.only;
    if (!db.scan(options.contentRoot, scan))
        return false;
    report.scanned = db.records().size();
    report.databaseIssues = db.issues();

    PackManifest previous;
    std::string manifestError;
    // A missing manifest is the first run, not a failure. Anything else -- a
    // corrupt or future-schema one -- is reported and then treated as empty,
    // because refusing to build is a worse answer than rebuilding.
    if (!previous.load(options.outputRoot, manifestError) &&
        fs::exists(options.outputRoot / content::kPackManifestName))
        log::warn("acp: ignoring unreadable manifest: %s", manifestError.c_str());

    // --- plan ---------------------------------------------------------------
    std::vector<Job> jobs;
    std::vector<AssetOutcome> immediate;
    std::unordered_set<std::string> published;
    PackManifest next;
    next.setDirectory(options.outputRoot);
    std::mutex nextMutex;

    // A run restricted to some assets is a PARTIAL run, and a partial run must
    // not speak for the ones it did not look at. It starts from the previous
    // manifest and replaces only what it rebuilds, and it removes nothing --
    // without this, `raven_acp build --filter viewmodel` published a manifest
    // containing 21 assets and deleted the other 606 files in the pack, because
    // every output it had not considered looked like an output whose source was
    // gone.
    const bool partial = !options.filter.empty() || !options.only.empty();
    if (partial) {
        for (const PackEntry& entry : previous.entries()) {
            published.insert(entry.output);
            next.add(entry);
        }
    }

    // Every output a SOURCE-stage record produces, across the whole database
    // rather than just this run's scope. An intermediate sitting in the tree at
    // one of these paths is a build artifact someone committed -- the pipeline
    // produces that file now -- so it is not published on top of the real one.
    // Computed over every record, not the in-scope ones, so a filtered run
    // cannot reach a different conclusion from a full one.
    std::unordered_map<std::string, std::string> producedFromSource;
    for (const Record& record : db.records()) {
        if (content::assetStage(record.logical) != content::AssetStage::Source)
            continue;
        const std::string output = content::intermediatePathFor(record.logical);
        if (!output.empty())
            producedFromSource.emplace(output, record.logical);
    }

    // Who produces what, decided for every record before any of it is built.
    //
    // This pass exists because two records CAN claim the same output, and in
    // this tree five of them do: `scenes/start_hall.map` is checked in beside
    // `scenes/start_hall.scn`, and the World row produces the first from the
    // second. Left alone, both are jobs, both write the same path from
    // different threads, and whichever finishes last wins -- so an edit to the
    // stale checked-in copy silently replaced a correctly cooked level, and
    // --check reported the pack current because both build keys matched their
    // own source. Stale level data shipping quietly is the exact failure this
    // pipeline exists to prevent.
    struct Planned {
        const Record* record = nullptr;
        const Exporter* exporter = nullptr;
        std::string output;
        bool intermediate = false;
    };
    std::vector<Planned> planned;
    planned.reserve(db.records().size());

    for (const Record& record : db.records()) {
        if (!options.filter.empty() &&
            record.logical.find(options.filter) == std::string::npos)
            continue;

        // A record can opt out. The resource database is where that decision
        // belongs: `meshes/viewmodels/arms_rig.glb` is a skinned rig, the
        // source gltf2ozz reads for the Skeleton and Animation rows, and it is
        // not a static mesh however much its extension says otherwise.
        if (content::settingBool(record.import, "skip", false)) {
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::Skipped;
            outcome.logical = record.logical;
            outcome.type = record.type;
            outcome.message =
                content::settingString(record.import, "skip_reason",
                                       "skipped by the resource database");
            immediate.push_back(std::move(outcome));
            continue;
        }

        // An asset that IS an intermediate -- a .map beside its .scn, an .ozz
        // clip -- is published by copy under its own name. Only source-stage
        // assets get their type's exporter.
        const bool alreadyIntermediate =
            content::assetStage(record.logical) == content::AssetStage::Intermediate;
        const Exporter* exporter = alreadyIntermediate
                                       ? &registry.publisher()
                                       : registry.find(record.type);
        if (!exporter) {
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::NoExporter;
            outcome.logical = record.logical;
            outcome.type = record.type;
            outcome.message = "no exporter for type '" +
                              std::string(content::assetTypeName(record.type)) +
                              "'";
            immediate.push_back(std::move(outcome));
            continue;
        }

        // A checked-in build artifact: the tree holds `scenes/start_hall.map`
        // and the World row produces exactly that from `start_hall.scn`. Both
        // used to be jobs writing the same path from different threads, so an
        // edit to the stale committed copy silently replaced a correctly cooked
        // level and --check called the pack current, because each build key
        // matched its own source.
        if (alreadyIntermediate) {
            const auto producer = producedFromSource.find(record.logical);
            if (producer != producedFromSource.end()) {
                AssetOutcome outcome;
                outcome.state = AssetOutcome::State::Skipped;
                outcome.logical = record.logical;
                outcome.output = record.logical;
                outcome.type = record.type;
                outcome.message =
                    "not published: the pipeline builds this from '" +
                    producer->second +
                    "'. The checked-in copy is a build artifact -- delete it.";
                immediate.push_back(std::move(outcome));
                continue;
            }
        }

        planned.push_back({&record, exporter, exporter->outputFor(record),
                           alreadyIntermediate});
    }

    // Two SOURCES claiming one output is a content bug with no safe answer --
    // whichever thread finishes last would win -- so it fails the build.
    {
        std::unordered_map<std::string, size_t> owner;
        std::vector<bool> dropped(planned.size(), false);
        for (size_t i = 0; i < planned.size(); ++i) {
            const auto [it, inserted] = owner.emplace(planned[i].output, i);
            if (inserted)
                continue;
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::Failed;
            outcome.logical = planned[i].record->logical;
            outcome.output = planned[i].output;
            outcome.type = planned[i].record->type;
            outcome.message = "output '" + planned[i].output +
                              "' is also produced from '" +
                              planned[it->second].record->logical +
                              "'; two sources cannot claim one output";
            dropped[i] = true;
            immediate.push_back(std::move(outcome));
        }
        std::vector<Planned> kept;
        kept.reserve(planned.size());
        for (size_t i = 0; i < planned.size(); ++i)
            if (!dropped[i])
                kept.push_back(planned[i]);
        planned = std::move(kept);
    }

    for (const Planned& entry : planned) {
        const Record& record = *entry.record;
        const Exporter* exporter = entry.exporter;
        const std::string& outputLogical = entry.output;
        const PackEntry* before = previous.bySource(record.logical);
        Job job;
        job.record = &record;
        job.exporter = exporter;
        job.outputLogical = outputLogical;
        // The dependency list the previous run recorded is this run's best
        // guess, and it only has to be right often enough: a dependency that
        // appears for the first time is discovered by the build that introduces
        // it and written into the manifest for the next one.
        if (before)
            job.dependencies = before->dependencies;
        job.key = buildKey(record, *exporter, dependencyHashes(db, job.dependencies));

        const fs::path outputPath = options.outputRoot / fs::path(outputLogical);
        std::error_code ec;
        const bool present = fs::is_regular_file(outputPath, ec);
        if (!options.force && before && before->buildKey == job.key && present) {
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::UpToDate;
            outcome.logical = record.logical;
            outcome.output = outputLogical;
            outcome.type = record.type;
            outcome.outputBytes = before->outputBytes;
            immediate.push_back(std::move(outcome));

            PackEntry entry = *before;
            next.set(std::move(entry));
            published.insert(outputLogical);
            continue;
        }
        published.insert(outputLogical);
        jobs.push_back(std::move(job));
    }

    for (const AssetOutcome& outcome : immediate) {
        if (outcome.state == AssetOutcome::State::UpToDate)
            ++report.upToDate;
        else if (outcome.state == AssetOutcome::State::NoExporter)
            ++report.noExporter;
        else if (outcome.state == AssetOutcome::State::Skipped)
            ++report.skipped;
        else if (outcome.state == AssetOutcome::State::Failed)
            // Two sources claiming one output. Counted here, before the build,
            // because it fails the run whichever mode this is.
            ++report.failed;
    }

    if (options.checkOnly) {
        for (const Job& job : jobs) {
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::Failed;
            outcome.logical = job.record->logical;
            outcome.output = job.outputLogical;
            outcome.type = job.record->type;
            outcome.message = "stale: not built from the current source";
            immediate.push_back(std::move(outcome));
            ++report.failed;
        }
        report.outcomes = std::move(immediate);
        report.seconds = now() - started;
        return report.succeeded();
    }

    if (options.dryRun) {
        for (const Job& job : jobs) {
            AssetOutcome outcome;
            outcome.state = AssetOutcome::State::Built;
            outcome.logical = job.record->logical;
            outcome.output = job.outputLogical;
            outcome.type = job.record->type;
            outcome.message = "would run " + std::string(job.exporter->name());
            immediate.push_back(std::move(outcome));
            ++report.built;
        }
        report.outcomes = std::move(immediate);
        report.seconds = now() - started;
        return true;
    }

    // --- export -------------------------------------------------------------
    const unsigned jobCount =
        options.jobs ? options.jobs
                     : std::max(1u, std::thread::hardware_concurrency());
    std::atomic<size_t> cursor{0};
    std::vector<AssetOutcome> outcomes(jobs.size());
    std::mutex progressMutex;
    std::atomic<size_t> done{0};

    const auto worker = [&] {
        for (;;) {
            const size_t index = cursor.fetch_add(1);
            if (index >= jobs.size())
                return;
            const Job& job = jobs[index];

            ExportContext context;
            context.contentRoot = options.contentRoot;
            context.outputRoot = options.outputRoot;
            context.record = job.record;
            context.sourcePath = job.record->sourcePath(options.contentRoot);
            context.outputPath = options.outputRoot / fs::path(job.outputLogical);
            context.outputLogical = job.outputLogical;

            // Created here rather than by each exporter: an exporter that
            // forgot would fail on a fresh output tree only, which is the
            // build nobody runs until CI does.
            std::error_code directoryError;
            if (!context.outputPath.parent_path().empty())
                fs::create_directories(context.outputPath.parent_path(),
                                       directoryError);

            const double jobStarted = now();
            const ExportResult exported = job.exporter->run(context);

            AssetOutcome& outcome = outcomes[index];
            outcome.logical = job.record->logical;
            outcome.output = job.outputLogical;
            outcome.type = job.record->type;
            outcome.warnings = exported.warnings;
            outcome.seconds = now() - jobStarted;

            if (!exported.ok) {
                outcome.state = AssetOutcome::State::Failed;
                outcome.message = exported.error;
            } else {
                outcome.state = AssetOutcome::State::Built;
                std::error_code ec;
                outcome.outputBytes =
                    static_cast<uint64_t>(fs::file_size(context.outputPath, ec));
                if (ec)
                    outcome.outputBytes = 0;

                PackEntry entry;
                entry.guid = job.record->guid;
                entry.type = job.record->type;
                entry.source = job.record->logical;
                entry.output = job.outputLogical;
                entry.outputBytes = outcome.outputBytes;
                // Re-keyed with the dependencies this run actually found, not
                // the ones it planned with. Otherwise an asset whose dependency
                // list just changed would be stale again on the very next run.
                entry.buildKey =
                    buildKey(*job.record, *job.exporter,
                             dependencyHashes(db, exported.dependencies));
                entry.dependencies = exported.dependencies;

                std::lock_guard<std::mutex> lock(nextMutex);
                next.set(std::move(entry));
            }

            if (progress) {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(outcome, ++done, jobs.size());
            } else {
                ++done;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(jobCount - 1);
    for (unsigned i = 1; i < jobCount; ++i)
        threads.emplace_back(worker);
    worker();
    for (std::thread& thread : threads)
        thread.join();

    for (AssetOutcome& outcome : outcomes) {
        if (outcome.state == AssetOutcome::State::Built)
            ++report.built;
        else if (outcome.state == AssetOutcome::State::Failed)
            ++report.failed;
        immediate.push_back(std::move(outcome));
    }

    // --- publish ------------------------------------------------------------
    // Outputs from a previous run whose source is gone. Left behind they would
    // be loaded by a game pointed at the pack -- the deleted asset would keep
    // working, and nobody would find out until a clean build.
    //
    // Only a full run may do this. A partial one has no idea whether an output
    // it did not consider still has a source, and answering "no" deletes the
    // pack.
    if (!partial) {
        for (const PackEntry& entry : previous.entries()) {
            if (published.count(entry.output))
                continue;
            std::error_code ec;
            if (fs::remove(options.outputRoot / fs::path(entry.output), ec))
                ++report.removed;
        }
    }

    std::string error;
    if (!next.save(options.outputRoot, error)) {
        log::error("acp: cannot write manifest: %s", error.c_str());
        report.outcomes = std::move(immediate);
        report.seconds = now() - started;
        return false;
    }

    report.outcomes = std::move(immediate);
    report.seconds = now() - started;
    return report.succeeded();
}

} // namespace eng::acp
