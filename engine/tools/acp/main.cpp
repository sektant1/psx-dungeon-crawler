// raven_acp -- the Asset Conditioning Pipeline, and the Resource Database
// Management Tool's command line half.
//
// Three subcommands, because the diagram has three things a person does:
//
//   raven_acp build     run the exporters, publish a pack the game can load
//   raven_acp db        inspect, stamp and validate the resource database
//   raven_acp formats   print the format table -- which extension is which row
//
// It is headless on purpose: CI conditions content without a GPU, and the
// editor calls the same functions in-process.

#include <eng/acp/Pipeline.h>
#include <eng/assets/AssetRoot.h>
#include <eng/content/ResourceDb.h>

#include <editor/content/SceneWorldExporter.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using eng::acp::AssetOutcome;
using eng::content::AssetType;

int usage()
{
    std::fprintf(
        stderr,
        "usage: raven_acp <command> [options]\n"
        "\n"
        "  build          condition every asset and publish the pack\n"
        "    --out <dir>       output pack directory (default build/cooked)\n"
        "    --root <dir>      content root (default: discovered)\n"
        "    --type <name>     only this asset type; repeatable\n"
        "    --filter <text>   only paths containing <text>\n"
        "    --jobs <n>        worker threads (default: cores)\n"
        "    --force           rebuild even when the build key matches\n"
        "    --stamp           write a .meta for every asset that lacks one\n"
        "    --dry-run         report what would build, touch nothing\n"
        "    --check           fail if anything is stale; what CI runs\n"
        "    --quiet           only print the summary and failures\n"
        "\n"
        "  db             the resource database\n"
        "    --root <dir>      content root\n"
        "    --stamp           write missing .meta sidecars\n"
        "    --type <name>     only this asset type; repeatable\n"
        "    --list            one line per record\n"
        "\n"
        "  formats        the asset format table: extensions and exporters\n");
    return 2;
}

const char* stateLabel(AssetOutcome::State state)
{
    switch (state) {
    case AssetOutcome::State::UpToDate:
        return "  ok  ";
    case AssetOutcome::State::Built:
        return "BUILT ";
    case AssetOutcome::State::Failed:
        return "FAILED";
    case AssetOutcome::State::NoExporter:
        return " skip ";
    case AssetOutcome::State::Skipped:
        return " skip ";
    }
    return "  ?   ";
}

std::string humanBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), value < 10.0 && unit > 0 ? "%.1f%s" : "%.0f%s",
                  value, units[unit]);
    return buffer;
}

struct Args {
    std::string command;
    std::string root;
    std::string out;
    std::string filter;
    std::vector<AssetType> types;
    unsigned jobs = 0;
    bool force = false;
    bool stamp = false;
    bool dryRun = false;
    bool check = false;
    bool quiet = false;
    bool list = false;
    bool bad = false;
};

Args parse(int argc, char** argv)
{
    Args args;
    if (argc < 2) {
        args.bad = true;
        return args;
    }
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto value = [&](std::string& target) {
            if (i + 1 >= argc) {
                args.bad = true;
                return;
            }
            target = argv[++i];
        };
        if (flag == "--root")
            value(args.root);
        else if (flag == "--out")
            value(args.out);
        else if (flag == "--filter")
            value(args.filter);
        else if (flag == "--type") {
            std::string name;
            value(name);
            const AssetType type = eng::content::assetTypeFromName(name);
            if (type == AssetType::Unknown) {
                std::fprintf(stderr, "raven_acp: unknown type '%s'\n",
                             name.c_str());
                args.bad = true;
            }
            args.types.push_back(type);
        } else if (flag == "--jobs") {
            std::string count;
            value(count);
            args.jobs = static_cast<unsigned>(std::atoi(count.c_str()));
        } else if (flag == "--force")
            args.force = true;
        else if (flag == "--stamp")
            args.stamp = true;
        else if (flag == "--dry-run")
            args.dryRun = true;
        else if (flag == "--check")
            args.check = true;
        else if (flag == "--quiet")
            args.quiet = true;
        else if (flag == "--list")
            args.list = true;
        else {
            std::fprintf(stderr, "raven_acp: unknown option '%s'\n", flag.c_str());
            args.bad = true;
        }
    }
    return args;
}

// The content root, resolved the same way every other consumer resolves it:
// through the manifest, so the pipeline and the game cannot disagree about
// where content lives.
std::filesystem::path contentRoot(const Args& args)
{
    if (!args.root.empty())
        return args.root;
    if (!eng::assets::init())
        return {};
    return eng::assets::root();
}

int commandFormats()
{
    std::printf("%-18s %-34s %-14s %s\n", "TYPE", "EXPORTER", "INTERMEDIATE",
                "SOURCE EXTENSIONS");
    for (const eng::content::AssetFormat& format : eng::content::assetFormats()) {
        std::string sources;
        for (const std::string& extension : format.source) {
            if (!sources.empty())
                sources += " ";
            sources += extension;
        }
        if (sources.empty())
            sources = "--";
        std::printf("%-18s %-34s %-14s %s\n",
                    std::string(format.name).c_str(),
                    format.exporter.empty() ? "(routed straight through)"
                                            : std::string(format.exporter).c_str(),
                    format.intermediate.empty()
                        ? "--"
                        : std::string(format.intermediate).c_str(),
                    sources.c_str());
    }
    return 0;
}

int commandDb(const Args& args)
{
    const std::filesystem::path root = contentRoot(args);
    if (root.empty()) {
        std::fprintf(stderr, "raven_acp: no content root\n");
        return 1;
    }

    eng::content::ResourceDb db;
    eng::content::ResourceDb::ScanOptions scan;
    scan.stampMissing = args.stamp;
    scan.types = args.types;
    if (!db.scan(root, scan))
        return 1;

    size_t stamped = 0;
    std::vector<size_t> perType(static_cast<size_t>(AssetType::Count), 0);
    for (const eng::content::Record& record : db.records()) {
        perType[static_cast<size_t>(record.type)] += 1;
        if (record.hasSidecar)
            ++stamped;
        if (args.list)
            std::printf("%s  %-16s %-8s %s\n",
                        eng::content::hashToHex(record.guid).c_str(),
                        std::string(eng::content::assetTypeName(record.type))
                            .c_str(),
                        record.hasSidecar ? "tracked" : "untracked",
                        record.logical.c_str());
    }

    if (!args.list) {
        for (size_t i = 0; i < perType.size(); ++i) {
            if (perType[i] == 0)
                continue;
            std::printf("%-18s %zu\n",
                        std::string(eng::content::assetTypeName(
                                        static_cast<AssetType>(i)))
                            .c_str(),
                        perType[i]);
        }
    }

    std::printf("\nresource db: %zu records, %zu with a .meta sidecar\n",
                db.records().size(), stamped);
    for (const eng::content::DbIssue& issue : db.issues())
        std::fprintf(stderr, "  issue: %s: %s\n", issue.logical.c_str(),
                     issue.message.c_str());
    return db.issues().empty() ? 0 : 1;
}

int commandBuild(const Args& args)
{
    const std::filesystem::path root = contentRoot(args);
    if (root.empty()) {
        std::fprintf(stderr, "raven_acp: no content root\n");
        return 1;
    }

    eng::acp::PipelineOptions options;
    options.contentRoot = root;
    options.outputRoot = args.out.empty()
                             ? eng::assets::project() / "build" / "cooked"
                             : std::filesystem::path(args.out);
    options.force = args.force;
    options.stamp = args.stamp;
    options.dryRun = args.dryRun;
    options.checkOnly = args.check;
    options.jobs = args.jobs;
    options.only = args.types;
    options.filter = args.filter;

    eng::acp::ExporterRegistry registry;
    eng::acp::registerBuiltinExporters(registry);
    // The World row. Registered here rather than in eng_acp because the .scn
    // cooker lives in game_content, and the engine must not link the editor's
    // scene format to condition a mesh.
    registry.add(ed::makeSceneWorldExporter());

    const bool quiet = args.quiet;
    eng::acp::PipelineReport report;
    const bool ok = eng::acp::run(
        options, registry, report,
        [quiet](const AssetOutcome& outcome, size_t done, size_t total) {
            const bool interesting =
                outcome.state == AssetOutcome::State::Failed ||
                !outcome.warnings.empty();
            if (quiet && !interesting)
                return;
            std::printf("[%zu/%zu] %s %-46s %8s %6.0fms\n", done, total,
                        stateLabel(outcome.state), outcome.logical.c_str(),
                        humanBytes(outcome.outputBytes).c_str(),
                        outcome.seconds * 1000.0);
            for (const std::string& warning : outcome.warnings)
                std::printf("          warning: %s\n", warning.c_str());
            if (outcome.state == AssetOutcome::State::Failed)
                std::fprintf(stderr, "          %s\n", outcome.message.c_str());
            std::fflush(stdout);
        });

    uint64_t packBytes = 0;
    for (const AssetOutcome& outcome : report.outcomes)
        packBytes += outcome.outputBytes;
    const bool partial = !args.filter.empty() || !args.types.empty();

    std::printf("\nraven_acp: %zu scanned, %zu built, %zu up to date, %zu skipped, "
                "%zu without an exporter, %zu failed, %zu stale outputs removed\n",
                report.scanned, report.built, report.upToDate, report.skipped,
                report.noExporter, report.failed, report.removed);
    // Labelled, because on a partial run this is the bytes of what was looked
    // at and not the size of the pack -- which still holds everything else.
    std::printf("           %s %s in %.2fs -> %s\n",
                partial ? "in scope" : "pack", humanBytes(packBytes).c_str(),
                report.seconds, options.outputRoot.string().c_str());

    // Skips carry a reason, and one of them asks for a file to be deleted.
    // They never reach the progress callback -- that only sees jobs -- so
    // without this the run reported "6 skipped" and nothing more.
    for (const AssetOutcome& outcome : report.outcomes) {
        if (outcome.state != AssetOutcome::State::Skipped ||
            outcome.message.empty())
            continue;
        std::printf("  skipped %-44s %s\n", outcome.logical.c_str(),
                    outcome.message.c_str());
    }

    for (const eng::content::DbIssue& issue : report.databaseIssues)
        std::fprintf(stderr, "  db issue: %s: %s\n", issue.logical.c_str(),
                     issue.message.c_str());

    if (!ok) {
        // Failures are reprinted at the end. During a parallel build the line
        // that mattered scrolled past 400 successes ago.
        std::fprintf(stderr, "\nfailed assets:\n");
        for (const AssetOutcome& outcome : report.outcomes)
            if (outcome.state == AssetOutcome::State::Failed)
                std::fprintf(stderr, "  %-46s %s\n", outcome.logical.c_str(),
                             outcome.message.c_str());
    }
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const Args args = parse(argc, argv);
    if (args.bad)
        return usage();
    if (args.command == "build")
        return commandBuild(args);
    if (args.command == "db")
        return commandDb(args);
    if (args.command == "formats")
        return commandFormats();
    return usage();
}
