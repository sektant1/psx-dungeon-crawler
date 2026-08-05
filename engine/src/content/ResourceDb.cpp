#include <eng/content/ResourceDb.h>

#include <eng/Log.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace fs = std::filesystem;

namespace eng::content {
namespace {

constexpr int kSidecarSchema = 1;
constexpr const char* kSidecarExtension = ".meta";

std::string toLogical(const fs::path& root, const fs::path& file)
{
    std::error_code ec;
    const fs::path relative = fs::relative(file, root, ec);
    if (ec)
        return file.generic_string();
    return relative.generic_string();
}

// TOML has one float type and one integer type, and toml++ reports which the
// document used. Preserving that distinction is what keeps a round-trip
// byte-identical: writing 1.0 back as `1` changes the meaning of a scale.
Setting settingFromNode(const toml::node& node, bool& ok)
{
    ok = true;
    if (const auto* value = node.as_boolean())
        return Setting::fromBool(value->get());
    if (const auto* value = node.as_integer())
        return Setting::fromInteger(value->get());
    if (const auto* value = node.as_floating_point())
        return Setting::fromNumber(value->get());
    if (const auto* value = node.as_string())
        return Setting::fromString(value->get());
    if (const auto* array = node.as_array()) {
        std::vector<std::string> list;
        for (const toml::node& entry : *array) {
            const auto* text = entry.as_string();
            if (!text) {
                ok = false;
                return {};
            }
            list.push_back(text->get());
        }
        return Setting::fromList(std::move(list));
    }
    ok = false;
    return {};
}

// Escaping is minimal on purpose: a setting value is authored content, and the
// two characters TOML basic strings cannot carry raw are the two handled here.
// Anything else -- a newline in a display name -- is a content bug the writer
// should not silently encode.
std::string quote(std::string_view text)
{
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string formatNumber(double value)
{
    std::ostringstream out;
    out.precision(17);
    out << value;
    std::string text = out.str();
    // TOML requires a float to look like one. Without this, 1.0 is written as
    // "1" and reads back as an integer, which changes the setting's kind and
    // therefore the build key -- an asset that rebuilt on every run.
    if (text.find_first_of(".eEnN") == std::string::npos)
        text += ".0";
    return text;
}

void writeSetting(std::ostream& out, const std::string& key, const Setting& value)
{
    out << key << " = ";
    switch (value.kind) {
    case Setting::Kind::Bool:
        out << (value.boolean ? "true" : "false");
        break;
    case Setting::Kind::Integer:
        out << value.integer;
        break;
    case Setting::Kind::Number:
        out << formatNumber(value.number);
        break;
    case Setting::Kind::String:
        out << quote(value.text);
        break;
    case Setting::Kind::StringList: {
        out << '[';
        for (size_t i = 0; i < value.list.size(); ++i) {
            if (i)
                out << ", ";
            out << quote(value.list[i]);
        }
        out << ']';
        break;
    }
    }
    out << '\n';
}

bool writeFileAtomic(const fs::path& path, const std::string& contents,
                     std::string& error)
{
    std::error_code ec;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "cannot create " + path.parent_path().string() + ": " +
                    ec.message();
            return false;
        }
    }
    const fs::path temp = fs::path(path).concat(".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot open " + temp.string() + " for writing";
            return false;
        }
        file << contents;
        if (!file) {
            error = "write to " + temp.string() + " failed";
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        error = "cannot replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace

Setting Setting::fromBool(bool value)
{
    Setting s;
    s.kind = Kind::Bool;
    s.boolean = value;
    return s;
}

Setting Setting::fromInteger(int64_t value)
{
    Setting s;
    s.kind = Kind::Integer;
    s.integer = value;
    return s;
}

Setting Setting::fromNumber(double value)
{
    Setting s;
    s.kind = Kind::Number;
    s.number = value;
    return s;
}

Setting Setting::fromString(std::string value)
{
    Setting s;
    s.kind = Kind::String;
    s.text = std::move(value);
    return s;
}

Setting Setting::fromList(std::vector<std::string> value)
{
    Setting s;
    s.kind = Kind::StringList;
    s.list = std::move(value);
    return s;
}

bool Setting::operator==(const Setting& other) const
{
    if (kind != other.kind)
        return false;
    switch (kind) {
    case Kind::Bool:
        return boolean == other.boolean;
    case Kind::Integer:
        return integer == other.integer;
    case Kind::Number:
        return number == other.number;
    case Kind::String:
        return text == other.text;
    case Kind::StringList:
        return list == other.list;
    }
    return false;
}

bool settingBool(const Settings& settings, std::string_view key, bool fallback)
{
    const auto it = settings.find(key);
    if (it == settings.end())
        return fallback;
    switch (it->second.kind) {
    case Setting::Kind::Bool:
        return it->second.boolean;
    case Setting::Kind::Integer:
        return it->second.integer != 0;
    default:
        return fallback;
    }
}

int64_t settingInteger(const Settings& settings, std::string_view key,
                       int64_t fallback)
{
    const auto it = settings.find(key);
    if (it == settings.end())
        return fallback;
    if (it->second.kind == Setting::Kind::Integer)
        return it->second.integer;
    if (it->second.kind == Setting::Kind::Number)
        return static_cast<int64_t>(it->second.number);
    return fallback;
}

double settingNumber(const Settings& settings, std::string_view key,
                     double fallback)
{
    const auto it = settings.find(key);
    if (it == settings.end())
        return fallback;
    if (it->second.kind == Setting::Kind::Number)
        return it->second.number;
    if (it->second.kind == Setting::Kind::Integer)
        return static_cast<double>(it->second.integer);
    return fallback;
}

std::string settingString(const Settings& settings, std::string_view key,
                          std::string_view fallback)
{
    const auto it = settings.find(key);
    if (it == settings.end() || it->second.kind != Setting::Kind::String)
        return std::string(fallback);
    return it->second.text;
}

std::vector<std::string> settingList(const Settings& settings,
                                     std::string_view key)
{
    const auto it = settings.find(key);
    if (it == settings.end() || it->second.kind != Setting::Kind::StringList)
        return {};
    return it->second.list;
}

Hash hashSettings(const Settings& settings)
{
    Hash h = kHashBasis;
    for (const auto& [key, value] : settings) {
        h = hashText(key, h);
        h = hashValue(static_cast<uint64_t>(value.kind), h);
        switch (value.kind) {
        case Setting::Kind::Bool:
            h = hashValue(value.boolean ? 1u : 0u, h);
            break;
        case Setting::Kind::Integer:
            h = hashValue(static_cast<uint64_t>(value.integer), h);
            break;
        case Setting::Kind::Number: {
            // The bit pattern, not the decimal spelling: 0.1 has no exact
            // decimal form and two writers of it must produce the same key.
            uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value.number));
            std::memcpy(&bits, &value.number, sizeof(bits));
            h = hashValue(bits, h);
            break;
        }
        case Setting::Kind::String:
            h = hashText(value.text, h);
            break;
        case Setting::Kind::StringList:
            for (const std::string& entry : value.list)
                h = hashText(entry, h);
            h = hashValue(value.list.size(), h);
            break;
        }
    }
    return h;
}

std::string Record::name() const
{
    if (!displayName.empty())
        return displayName;
    return fs::path(logical).stem().string();
}

fs::path Record::sidecarPath(const fs::path& root) const
{
    return sourcePath(root).concat(kSidecarExtension);
}

fs::path Record::sourcePath(const fs::path& root) const
{
    return root / fs::path(logical);
}

Settings defaultSettings(AssetType type)
{
    Settings settings;
    switch (type) {
    case AssetType::Mesh:
        // The names match eng::ModelImportOptions field for field, written out
        // so an artist can see and change them without reading C++.
        //
        // `source`, not ModelImportOptions' own `bottom_center` default: every
        // shipped mesh in this engine is loaded with PivotMode::Source, because
        // the kit and prop loaders place geometry with their own bake matrix
        // and a re-pivoted mesh would arrive somewhere else. Geometry is baked
        // into a .rmesh, so this is not a preference -- a conditioned mesh whose
        // pivot disagrees with the call site is refused by loadStaticModel()
        // and quietly costs a full Assimp import instead.
        settings["pivot"] = Setting::fromString("source");
        settings["metres_per_source_unit"] = Setting::fromNumber(1.0);
        settings["texcoord_v"] = Setting::fromString("format_default");
        settings["generate_collision"] = Setting::fromBool(true);
        break;
    case AssetType::Texture:
        // "none" publishes the source image unchanged. The shipped look is
        // nearest-neighbour pixel art and a block codec visibly alters it, so
        // compression is opt-in per asset -- which is where the book puts it
        // too (1.6.4: "the animator's choice of compression technique and
        // level"). "auto", "bc1" and "bc3" are the alternatives.
        settings["compression"] = Setting::fromString("none");
        settings["generate_mips"] = Setting::fromBool(false);
        // Checked, never applied: a texture the pipeline silently resized is
        // how a UI atlas stops lining up. Over budget is a warning to fix at
        // the source.
        settings["max_size"] = Setting::fromInteger(2048);
        settings["srgb"] = Setting::fromBool(true);
        break;
    case AssetType::World:
        settings["cook"] = Setting::fromBool(true);
        break;
    case AssetType::Sound:
        settings["streaming"] = Setting::fromBool(false);
        break;
    default:
        break;
    }
    return settings;
}

Hash ResourceDb::guidForLogical(std::string_view logical)
{
    return hashText(logical);
}

bool readSidecar(const fs::path& path, Record& out, std::string& error)
{
    const toml::parse_result parsed = toml::parse_file(path.string());
    if (!parsed) {
        error = std::string(parsed.error().description());
        return false;
    }
    const toml::table& table = parsed.table();

    if (const auto* schema = table["schema"].as_integer()) {
        if (schema->get() != kSidecarSchema) {
            error = "unsupported schema " + std::to_string(schema->get());
            return false;
        }
    }

    if (const auto* guid = table["guid"].as_string()) {
        if (!hashFromHex(guid->get(), out.guid)) {
            error = "guid '" + guid->get() + "' is not 16 hex digits";
            return false;
        }
    }
    if (const auto* type = table["type"].as_string())
        out.type = assetTypeFromName(type->get());
    if (const auto* name = table["name"].as_string())
        out.displayName = name->get();
    if (const auto* tags = table["tags"].as_array()) {
        out.tags.clear();
        for (const toml::node& entry : *tags)
            if (const auto* text = entry.as_string())
                out.tags.push_back(text->get());
    }

    out.import.clear();
    if (const auto* import = table["import"].as_table()) {
        for (const auto& [key, node] : *import) {
            bool ok = false;
            Setting value = settingFromNode(node, ok);
            if (!ok) {
                error = "import." + std::string(key.str()) +
                        " is not a bool, number, string or string list";
                return false;
            }
            out.import.emplace(std::string(key.str()), std::move(value));
        }
    }
    return true;
}

bool ResourceDb::writeSidecar(const Record& record, std::string& error) const
{
    std::ostringstream out;
    out << "# Resource database record. Authored metadata only -- the source\n"
           "# hash, the outputs and the build state are derived and live in\n"
           "# build/acp, not here.\n";
    out << "schema = " << kSidecarSchema << '\n';
    out << "guid = " << quote(hashToHex(record.guid)) << '\n';
    out << "type = " << quote(assetTypeName(record.type)) << '\n';
    if (!record.displayName.empty())
        out << "name = " << quote(record.displayName) << '\n';
    if (!record.tags.empty()) {
        out << "tags = [";
        for (size_t i = 0; i < record.tags.size(); ++i) {
            if (i)
                out << ", ";
            out << quote(record.tags[i]);
        }
        out << "]\n";
    }
    if (!record.import.empty()) {
        out << "\n[import]\n";
        for (const auto& [key, value] : record.import)
            writeSetting(out, key, value);
    }
    return writeFileAtomic(record.sidecarPath(mRoot), out.str(), error);
}

void ResourceDb::loadRecord(const fs::path& file, const std::string& logical,
                            const ScanOptions& options)
{
    Record record;
    record.logical = logical;
    record.type = classifyAsset(logical);

    if (!options.types.empty() &&
        std::find(options.types.begin(), options.types.end(), record.type) ==
            options.types.end())
        return;

    const fs::path sidecar = record.sidecarPath(mRoot);
    std::error_code ec;
    if (fs::is_regular_file(sidecar, ec)) {
        std::string error;
        if (readSidecar(sidecar, record, error)) {
            record.hasSidecar = true;
        } else {
            mIssues.push_back({logical, "sidecar: " + error});
            // Keep the record. An asset with a broken sidecar is still an
            // asset; it just gets defaults and a reported issue, which is what
            // lets one bad file be fixed instead of hiding the other 400.
            record.type = classifyAsset(logical);
            record.import = defaultSettings(record.type);
        }
    } else {
        record.import = defaultSettings(record.type);
    }

    if (record.guid == 0)
        record.guid = guidForLogical(logical);

    record.sourceHash = hashFile(file);
    record.sourceBytes = static_cast<uint64_t>(fs::file_size(file, ec));
    if (ec)
        record.sourceBytes = 0;
    if (record.sourceHash == 0)
        mIssues.push_back({logical, "source file could not be read"});

    if (options.stampMissing && !record.hasSidecar) {
        std::string error;
        if (writeSidecar(record, error))
            record.hasSidecar = true;
        else
            mIssues.push_back({logical, "cannot stamp sidecar: " + error});
    }

    const auto existing = mByGuid.find(record.guid);
    if (existing != mByGuid.end()) {
        // Two sidecars carrying the same guid. Almost always a copy-paste of a
        // .meta alongside a duplicated asset, and left unreported it makes the
        // manifest silently drop one of them.
        mIssues.push_back(
            {logical, "guid " + hashToHex(record.guid) + " already used by " +
                          mRecords[existing->second].logical});
        return;
    }

    mByGuid.emplace(record.guid, mRecords.size());
    mByLogical.emplace(record.logical, mRecords.size());
    mRecords.push_back(std::move(record));
}

bool ResourceDb::scan(const fs::path& root) { return scan(root, ScanOptions{}); }

bool ResourceDb::scan(const fs::path& root, const ScanOptions& options)
{
    mRoot = root;
    mRecords.clear();
    mIssues.clear();
    mByGuid.clear();
    mByLogical.clear();

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        log::error("resourcedb: '%s' is not a directory", root.string().c_str());
        return false;
    }

    // Collected and sorted before any record is built: a scan whose record
    // order depends on the filesystem's readdir order produces a different
    // manifest on every machine, and the manifest is a build output that has
    // to compare clean.
    std::vector<fs::path> files;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        log::error("resourcedb: cannot walk '%s': %s", root.string().c_str(),
                   ec.message().c_str());
        return false;
    }
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            mIssues.push_back({"", "walk error: " + ec.message()});
            ec.clear();
            continue;
        }
        const fs::path& path = it->path();
        const std::string name = path.filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) {
            const std::string logical = toLogical(root, path);
            if (logical.find('/') == std::string::npos &&
                ignoredContentDir(logical))
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec))
            continue;
        if (path.extension() == kSidecarExtension)
            continue;
        if (ignoredContentFile(toLogical(root, path)))
            continue;
        files.push_back(path);
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& file : files)
        loadRecord(file, toLogical(root, file), options);
    return true;
}

const Record* ResourceDb::find(Hash guid) const
{
    const auto it = mByGuid.find(guid);
    return it == mByGuid.end() ? nullptr : &mRecords[it->second];
}

const Record* ResourceDb::findLogical(std::string_view logical) const
{
    const auto it = mByLogical.find(logical);
    return it == mByLogical.end() ? nullptr : &mRecords[it->second];
}

std::vector<const Record*> ResourceDb::byType(AssetType type) const
{
    std::vector<const Record*> out;
    for (const Record& record : mRecords)
        if (record.type == type)
            out.push_back(&record);
    return out;
}

Record* ResourceDb::mutableRecord(Hash guid)
{
    const auto it = mByGuid.find(guid);
    return it == mByGuid.end() ? nullptr : &mRecords[it->second];
}

} // namespace eng::content
