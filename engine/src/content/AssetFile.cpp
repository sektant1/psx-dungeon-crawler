#include <eng/content/AssetFile.h>

#include <cstring>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace eng::content {
namespace {

// A pool string long enough to be a length field misread as text, or a pool
// large enough to be a corrupt count, is rejected rather than allocated. The
// reader is fed build outputs, but a truncated one is exactly what a killed
// build leaves behind, and "allocate 4 GB then fail" is a bad way to find out.
constexpr uint32_t kMaxPoolEntries = 1u << 20;
constexpr uint32_t kMaxStringBytes = 1u << 20;

void putU16(std::string& out, uint16_t v)
{
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}

void putU32(std::string& out, uint32_t v)
{
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
    out += static_cast<char>((v >> 16) & 0xFF);
    out += static_cast<char>((v >> 24) & 0xFF);
}

bool takeU16(const std::vector<uint8_t>& data, size_t& at, uint16_t& out)
{
    if (at + 2 > data.size())
        return false;
    out = static_cast<uint16_t>(data[at]) |
          static_cast<uint16_t>(static_cast<uint16_t>(data[at + 1]) << 8);
    at += 2;
    return true;
}

bool takeU32(const std::vector<uint8_t>& data, size_t& at, uint32_t& out)
{
    if (at + 4 > data.size())
        return false;
    out = static_cast<uint32_t>(data[at]) |
          (static_cast<uint32_t>(data[at + 1]) << 8) |
          (static_cast<uint32_t>(data[at + 2]) << 16) |
          (static_cast<uint32_t>(data[at + 3]) << 24);
    at += 4;
    return true;
}

} // namespace

bool writeAssetFile(const fs::path& path, const char magic[8], uint16_t version,
                     const io::ByteWriter& body, std::string& error)
{
    std::string out;
    out.reserve(body.size() + 64);
    out.append(magic, 8);
    putU16(out, version);
    putU16(out, 0); // flags, reserved

    putU32(out, static_cast<uint32_t>(body.pool().size()));
    for (const std::string& entry : body.pool()) {
        putU32(out, static_cast<uint32_t>(entry.size()));
        out.append(entry);
    }

    putU32(out, static_cast<uint32_t>(body.size()));
    out.append(reinterpret_cast<const char*>(body.bytes().data()), body.size());

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
        file.write(out.data(), static_cast<std::streamsize>(out.size()));
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

bool readAssetFile(const fs::path& path, const char magic[8],
                    AssetFileBody& out, std::string& error)
{
    out = {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "cannot open " + path.string();
        return false;
    }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size < 0) {
        error = "cannot size " + path.string();
        return false;
    }
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!file && !file.eof()) {
        error = "cannot read " + path.string();
        return false;
    }

    size_t at = 0;
    if (data.size() < 8 || std::memcmp(data.data(), magic, 8) != 0) {
        error = path.filename().string() + " is not a " +
                std::string(magic, 8) + " file";
        return false;
    }
    at = 8;

    uint16_t flags = 0;
    if (!takeU16(data, at, out.version) || !takeU16(data, at, flags)) {
        error = "truncated header";
        return false;
    }
    if (flags != 0) {
        error = "unknown header flags";
        return false;
    }

    uint32_t poolCount = 0;
    if (!takeU32(data, at, poolCount) || poolCount > kMaxPoolEntries) {
        error = "bad string pool count";
        return false;
    }
    out.pool.reserve(poolCount);
    for (uint32_t i = 0; i < poolCount; ++i) {
        uint32_t length = 0;
        if (!takeU32(data, at, length) || length > kMaxStringBytes ||
            at + length > data.size()) {
            error = "truncated string pool";
            return false;
        }
        out.pool.emplace_back(reinterpret_cast<const char*>(data.data() + at),
                              length);
        at += length;
    }

    uint32_t payload = 0;
    if (!takeU32(data, at, payload) || at + payload > data.size()) {
        error = "truncated payload";
        return false;
    }
    out.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(at),
                     data.begin() + static_cast<std::ptrdiff_t>(at + payload));
    return true;
}

bool assetFileMatches(const fs::path& path, const char magic[8])
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    char header[8] = {};
    file.read(header, 8);
    return file.gcount() == 8 && std::memcmp(header, magic, 8) == 0;
}

} // namespace eng::content
