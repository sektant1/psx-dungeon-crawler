#include <eng/content/DataAsset.h>

#include <eng/content/AssetFile.h>

#include <cstring>

namespace fs = std::filesystem;

namespace eng::content {
namespace {

// Depth and width bounds, so a corrupt or truncated file fails instead of
// recursing until the stack runs out. Authored content nests four deep at the
// most ([[effect]] -> [[effect.emitter]] -> an array -> a number).
constexpr uint32_t kMaxDepth = 32;
constexpr uint32_t kMaxChildren = 1u << 20;

const DataValue& nullValue()
{
    static const DataValue value;
    return value;
}

void writeValue(io::ByteWriter& out, const DataValue& value, uint32_t depth)
{
    out.u8(static_cast<uint8_t>(value.kind()));
    switch (value.kind()) {
    case DataValue::Kind::Null:
        break;
    case DataValue::Kind::Bool:
        out.u8(value.asBool() ? 1u : 0u);
        break;
    case DataValue::Kind::Integer:
        out.u64(static_cast<uint64_t>(value.asInteger()));
        break;
    case DataValue::Kind::Number: {
        uint64_t bits = 0;
        const double number = value.asNumber();
        static_assert(sizeof(bits) == sizeof(number));
        std::memcpy(&bits, &number, sizeof(bits));
        out.u64(bits);
        break;
    }
    case DataValue::Kind::String:
        out.str(value.asString());
        break;
    case DataValue::Kind::Array: {
        const DataArray& array = value.asArray();
        out.u32(static_cast<uint32_t>(array.size()));
        if (depth < kMaxDepth)
            for (const DataValue& entry : array)
                writeValue(out, entry, depth + 1);
        break;
    }
    case DataValue::Kind::Table: {
        const DataTable& table = value.asTable();
        out.u32(static_cast<uint32_t>(table.size()));
        if (depth < kMaxDepth)
            for (const auto& [key, entry] : table) {
                out.str(key);
                writeValue(out, entry, depth + 1);
            }
        break;
    }
    }
}

bool readValue(io::ByteReader& in, DataValue& out, uint32_t depth)
{
    if (depth > kMaxDepth)
        return false;
    const uint8_t kind = in.u8();
    if (!in.ok())
        return false;
    switch (static_cast<DataValue::Kind>(kind)) {
    case DataValue::Kind::Null:
        out = DataValue();
        return true;
    case DataValue::Kind::Bool:
        out = DataValue::makeBool(in.u8() != 0);
        return in.ok();
    case DataValue::Kind::Integer:
        out = DataValue::makeInteger(static_cast<int64_t>(in.u64()));
        return in.ok();
    case DataValue::Kind::Number: {
        const uint64_t bits = in.u64();
        double number = 0.0;
        std::memcpy(&number, &bits, sizeof(number));
        out = DataValue::makeNumber(number);
        return in.ok();
    }
    case DataValue::Kind::String: {
        const std::string& text = in.str();
        if (!in.ok())
            return false;
        out = DataValue::makeString(text);
        return true;
    }
    case DataValue::Kind::Array: {
        const uint32_t count = in.u32();
        if (!in.ok() || count > kMaxChildren)
            return false;
        DataArray array(count);
        for (DataValue& entry : array)
            if (!readValue(in, entry, depth + 1))
                return false;
        out = DataValue::makeArray(std::move(array));
        return true;
    }
    case DataValue::Kind::Table: {
        const uint32_t count = in.u32();
        if (!in.ok() || count > kMaxChildren)
            return false;
        DataTable table(count);
        for (auto& [key, entry] : table) {
            key = in.str();
            if (!in.ok() || !readValue(in, entry, depth + 1))
                return false;
        }
        out = DataValue::makeTable(std::move(table));
        return true;
    }
    }
    return false;
}

} // namespace

DataValue::DataValue() = default;
DataValue::~DataValue() = default;
DataValue::DataValue(DataValue&&) noexcept = default;
DataValue& DataValue::operator=(DataValue&&) noexcept = default;

DataValue::DataValue(const DataValue& other)
    : mKind(other.mKind), mBool(other.mBool), mInteger(other.mInteger),
      mNumber(other.mNumber), mString(other.mString),
      mArray(other.mArray ? std::make_unique<DataArray>(*other.mArray) : nullptr),
      mTable(other.mTable ? std::make_unique<DataTable>(*other.mTable) : nullptr)
{
}

DataValue& DataValue::operator=(const DataValue& other)
{
    if (this != &other) {
        DataValue copy(other);
        *this = std::move(copy);
    }
    return *this;
}

DataValue DataValue::makeBool(bool value)
{
    DataValue out;
    out.mKind = Kind::Bool;
    out.mBool = value;
    return out;
}

DataValue DataValue::makeInteger(int64_t value)
{
    DataValue out;
    out.mKind = Kind::Integer;
    out.mInteger = value;
    return out;
}

DataValue DataValue::makeNumber(double value)
{
    DataValue out;
    out.mKind = Kind::Number;
    out.mNumber = value;
    return out;
}

DataValue DataValue::makeString(std::string value)
{
    DataValue out;
    out.mKind = Kind::String;
    out.mString = std::move(value);
    return out;
}

DataValue DataValue::makeArray(DataArray value)
{
    DataValue out;
    out.mKind = Kind::Array;
    out.mArray = std::make_unique<DataArray>(std::move(value));
    return out;
}

DataValue DataValue::makeTable(DataTable value)
{
    DataValue out;
    out.mKind = Kind::Table;
    out.mTable = std::make_unique<DataTable>(std::move(value));
    return out;
}

bool DataValue::asBool(bool fallback) const
{
    if (mKind == Kind::Bool)
        return mBool;
    if (mKind == Kind::Integer)
        return mInteger != 0;
    return fallback;
}

int64_t DataValue::asInteger(int64_t fallback) const
{
    if (mKind == Kind::Integer)
        return mInteger;
    if (mKind == Kind::Number)
        return static_cast<int64_t>(mNumber);
    return fallback;
}

double DataValue::asNumber(double fallback) const
{
    if (mKind == Kind::Number)
        return mNumber;
    if (mKind == Kind::Integer)
        return static_cast<double>(mInteger);
    return fallback;
}

const std::string& DataValue::asString() const
{
    static const std::string empty;
    return mKind == Kind::String ? mString : empty;
}

const DataArray& DataValue::asArray() const
{
    static const DataArray empty;
    return mArray ? *mArray : empty;
}

const DataTable& DataValue::asTable() const
{
    static const DataTable empty;
    return mTable ? *mTable : empty;
}

const DataValue& DataValue::operator[](std::string_view key) const
{
    if (!mTable)
        return nullValue();
    for (const auto& [name, value] : *mTable)
        if (name == key)
            return value;
    return nullValue();
}

const DataValue& DataValue::at(std::string_view path) const
{
    const DataValue* current = this;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t dot = path.find('.', start);
        const std::string_view segment =
            path.substr(start, dot == std::string_view::npos ? std::string_view::npos
                                                             : dot - start);
        current = &(*current)[segment];
        if (current->isNull() || dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    return *current;
}

bool writeDataAsset(const fs::path& path, const DataAsset& asset,
                    std::string& error)
{
    io::ByteWriter out;
    out.str(asset.sourcePath);
    writeValue(out, asset.root, 0);
    return writeAssetFile(path, kDataAssetMagic, kDataAssetVersion, out, error);
}

bool readDataAsset(const fs::path& path, DataAsset& out, std::string& error)
{
    out = {};
    AssetFileBody body;
    if (!readAssetFile(path, kDataAssetMagic, body, error))
        return false;
    if (body.version != kDataAssetVersion) {
        error = "data asset version " + std::to_string(body.version) +
                ", this build reads " + std::to_string(kDataAssetVersion);
        return false;
    }
    io::ByteReader in = body.reader();
    out.sourcePath = in.str();
    if (!in.ok() || !readValue(in, out.root, 0)) {
        error = "truncated or malformed data asset payload";
        return false;
    }
    return true;
}

bool isDataAsset(const fs::path& path)
{
    return assetFileMatches(path, kDataAssetMagic);
}

} // namespace eng::content
