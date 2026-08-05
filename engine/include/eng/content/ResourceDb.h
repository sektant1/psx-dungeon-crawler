#pragma once

#include <eng/content/AssetType.h>
#include <eng/content/ContentHash.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// The resource database (Game Engine Architecture, 4th ed., 1.6.4).
//
// Every asset carries metadata the source file itself cannot express: a stable
// id that survives a rename, which conditioner owns it, and the import
// decisions an artist made -- the scale a model was authored at, whether an
// animation loops, how hard a texture may be compressed. Until now this engine
// had nowhere to put any of that, so it lived as defaults in C++
// (ModelImportOptions), as ad-hoc keys in whatever TOML happened to reference
// the asset, or nowhere at all.
//
// Storage is one text sidecar per asset -- `lamp.obj` gets `lamp.obj.meta` --
// checked into git, which is the book's second option and the right one here:
// there is no server to run, a rename moves the metadata with the file in one
// commit, and two people adding assets never touch the same line. The
// alternative, one central database file, makes every asset addition a merge
// conflict.
//
// Nothing in this header knows how to *build* anything. It is in eng_core so
// the game, the editor, the pipeline and the tests all read the same records;
// the conditioners live in eng_acp, above it.
namespace eng::content {

// One import setting. Typed rather than a string bag so a sidecar round-trips
// through TOML unchanged and the editor can show a checkbox for a bool instead
// of a text field containing the word "true".
struct Setting {
    enum class Kind : uint8_t { Bool, Integer, Number, String, StringList };

    Kind kind = Kind::String;
    bool boolean = false;
    int64_t integer = 0;
    double number = 0.0;
    std::string text;
    std::vector<std::string> list;

    static Setting fromBool(bool value);
    static Setting fromInteger(int64_t value);
    static Setting fromNumber(double value);
    static Setting fromString(std::string value);
    static Setting fromList(std::vector<std::string> value);

    bool operator==(const Setting& other) const;
};

// Ordered, so hashing the settings is deterministic without sorting at every
// call site -- the build key depends on it.
using Settings = std::map<std::string, Setting, std::less<>>;

bool settingBool(const Settings&, std::string_view key, bool fallback);
int64_t settingInteger(const Settings&, std::string_view key, int64_t fallback);
double settingNumber(const Settings&, std::string_view key, double fallback);
std::string settingString(const Settings&, std::string_view key,
                          std::string_view fallback);
std::vector<std::string> settingList(const Settings&, std::string_view key);

// Folds every key and value in, in key order. Part of every build key: change
// a model's authored scale and the mesh rebuilds without the file changing.
Hash hashSettings(const Settings&);

// One asset. `logical` is the content-root-relative path with forward slashes
// -- the same string assets::resolve() takes -- and is the record's human
// identity; `guid` is its machine identity and is what the manifest and any
// future reference-by-id use.
struct Record {
    Hash guid = 0;
    AssetType type = AssetType::Unknown;
    std::string logical;
    // What a browser shows. Empty means "use the file stem", which is what
    // almost every asset wants; it exists for the ones where the filename is
    // an import artifact ("mesh_003").
    std::string displayName;
    std::vector<std::string> tags;
    Settings import;

    // Derived by scan(), never stored in the sidecar. A sidecar that recorded
    // the source hash would be a merge conflict on every edit and would go
    // stale the moment anyone touched the file outside the tool.
    Hash sourceHash = 0;
    uint64_t sourceBytes = 0;
    bool hasSidecar = false;
    bool sourceMissing = false;

    std::string name() const;
    std::filesystem::path sidecarPath(const std::filesystem::path& root) const;
    std::filesystem::path sourcePath(const std::filesystem::path& root) const;
};

// A problem found while scanning. Reported rather than thrown: a content tree
// with one bad sidecar must still be usable for every other asset in it.
struct DbIssue {
    std::string logical;
    std::string message;
};

class ResourceDb {
public:
    struct ScanOptions {
        // Write a sidecar for every asset that does not have one, assigning a
        // guid. Off by default: a read-only consumer (the game, a test) must
        // never dirty the working tree, and `raven_acp --stamp` is the one
        // caller that means to.
        bool stampMissing = false;
        // Types to scan. Empty means every type, including Unknown -- an
        // unclassified file is still tracked so a dependency on it is visible.
        std::vector<AssetType> types;
    };

    // Walks `root`, classifies every file, and loads the sidecar beside each.
    // Directories named by ignoredContentDir() are skipped whole; so is any
    // `.meta` itself, and anything under a dot-directory.
    //
    // Returns false only when the root cannot be read. A malformed sidecar is
    // an issue and a record with default settings, not a failed scan.
    bool scan(const std::filesystem::path& root, const ScanOptions& options);
    bool scan(const std::filesystem::path& root);

    const std::filesystem::path& root() const { return mRoot; }
    const std::vector<Record>& records() const { return mRecords; }
    const std::vector<DbIssue>& issues() const { return mIssues; }

    const Record* find(Hash guid) const;
    const Record* findLogical(std::string_view logical) const;
    std::vector<const Record*> byType(AssetType type) const;

    // In-place edit, for the management tool. Returns nullptr for an unknown
    // guid; the caller writes the sidecar back with writeSidecar().
    Record* mutableRecord(Hash guid);

    // Serialises one record's authored fields to its sidecar, atomically
    // (temp file, then rename), so an interrupted write cannot leave a
    // half-parsed .meta in the tree.
    bool writeSidecar(const Record& record, std::string& error) const;

    // The id a path gets when it is first stamped: the hash of its logical
    // path. Deterministic so two people who add the same asset independently
    // produce the same id, and stable afterwards because the sidecar keeps it
    // through the rename that would change this answer.
    static Hash guidForLogical(std::string_view logical);

private:
    void loadRecord(const std::filesystem::path& file, const std::string& logical,
                    const ScanOptions& options);

    std::filesystem::path mRoot;
    std::vector<Record> mRecords;
    std::vector<DbIssue> mIssues;
    std::map<Hash, size_t> mByGuid;
    std::map<std::string, size_t, std::less<>> mByLogical;
};

// Reads a single sidecar into `out` (leaving `out.logical` alone). Exposed for
// the tests and for tools that hold one asset rather than a tree.
bool readSidecar(const std::filesystem::path& path, Record& out,
                 std::string& error);

// The default import settings for a type: what a sidecar contains when the
// pipeline stamps one for the first time. Keeping them here rather than in each
// conditioner is what lets a stamped tree be reviewed before anything is built.
Settings defaultSettings(AssetType type);

} // namespace eng::content
