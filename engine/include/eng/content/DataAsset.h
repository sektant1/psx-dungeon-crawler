#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The conditioned form of the diagram's three authored-data rows: Particle
// System (`.rpfx`), Sound Bank (`.rbank`) and Game Obj. Templates (`.rtpl`).
//
// All three are authored as TOML and all three are read at startup, which is
// exactly the situation section 1.6.2 describes: "the DCC application's file
// format is often too slow to read at runtime". `config/kit.toml` alone is
// ~1600 lines of tables the generator walks before the first frame, and
// `particles/sprite_sheets.toml` carries 304 flipbook definitions.
//
// What it is NOT is three new schemas. A binary Particle System struct and a
// binary Sound Bank struct would each be a second definition of a file
// ParticleLibrary and Audio already parse, and the two would drift the first
// time anyone added a key. Instead the exporter conditions the *document*: TOML
// in, an ordered typed tree out, one binary encoding for all three. The runtime
// reader hands back the same key/value tree the TOML reader produces, so the
// consumers are unchanged and a new authoring key needs no pipeline work at
// all.
//
// Order is preserved because TOML arrays-of-tables are ordered and the content
// depends on it: `particles/*.toml` documents that later files win a duplicate
// stem, which is only meaningful if the order survives.
namespace eng::content {

inline constexpr char kDataAssetMagic[8] = {'R', 'A', 'V', 'E',
                                            'N', 'D', 'A', 'T'};
inline constexpr uint16_t kDataAssetVersion = 1;

inline constexpr const char* kParticleAssetExtension = ".rpfx";
inline constexpr const char* kSoundBankAssetExtension = ".rbank";
inline constexpr const char* kObjectTemplateAssetExtension = ".rtpl";
inline constexpr const char* kAnimationTreeAssetExtension = ".rtree";

class DataValue;

// Ordered key/value pairs, not a map: see the note about duplicate stems above.
// Lookup is linear, which is right for the sizes involved -- the widest table
// in this content tree has 40 keys, and a hash map would cost more to build
// than it saves.
using DataTable = std::vector<std::pair<std::string, DataValue>>;
using DataArray = std::vector<DataValue>;

class DataValue {
public:
    enum class Kind : uint8_t { Null, Bool, Integer, Number, String, Array, Table };

    DataValue();
    ~DataValue();
    DataValue(const DataValue&);
    DataValue& operator=(const DataValue&);
    DataValue(DataValue&&) noexcept;
    DataValue& operator=(DataValue&&) noexcept;

    static DataValue makeBool(bool);
    static DataValue makeInteger(int64_t);
    static DataValue makeNumber(double);
    static DataValue makeString(std::string);
    static DataValue makeArray(DataArray);
    static DataValue makeTable(DataTable);

    Kind kind() const { return mKind; }
    bool isNull() const { return mKind == Kind::Null; }

    // Typed reads with a fallback. Integer and Number convert into each other,
    // because TOML's `1` and `1.0` are the same authored intent and no consumer
    // in this engine cares which the author typed.
    bool asBool(bool fallback = false) const;
    int64_t asInteger(int64_t fallback = 0) const;
    double asNumber(double fallback = 0.0) const;
    const std::string& asString() const;
    const DataArray& asArray() const;
    const DataTable& asTable() const;

    // Table lookup by key, and dotted-path lookup ("mixer.master_db"). Both
    // return a null DataValue when absent, so a chain of reads never needs a
    // presence check between links.
    const DataValue& operator[](std::string_view key) const;
    const DataValue& at(std::string_view path) const;

private:
    Kind mKind = Kind::Null;
    bool mBool = false;
    int64_t mInteger = 0;
    double mNumber = 0.0;
    std::string mString;
    // Indirect so DataValue can contain itself. unique_ptr rather than a
    // variant of vectors because the recursive type has to be complete at the
    // member declaration, and this keeps a scalar DataValue two words wide.
    std::unique_ptr<DataArray> mArray;
    std::unique_ptr<DataTable> mTable;
};

struct DataAsset {
    // The logical path this was conditioned from, for diagnostics and for the
    // editor's "what produced this?" column.
    std::string sourcePath;
    DataValue root;
};

bool writeDataAsset(const std::filesystem::path& path, const DataAsset& asset,
                    std::string& error);
bool readDataAsset(const std::filesystem::path& path, DataAsset& out,
                   std::string& error);
bool isDataAsset(const std::filesystem::path& path);

} // namespace eng::content
