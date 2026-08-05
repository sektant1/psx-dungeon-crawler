#pragma once

#include <eng/io/ByteStream.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The container every conditioned asset is written in.
//
// One header, one string pool, one payload -- so `.rmesh` and `.rtex` do not
// each invent a magic number, a version check and an atomic write, and so a
// future format gets all three for free. It is deliberately the same shape as
// the .map file (8-byte magic, u16 version, little-endian body through
// eng::io::ByteWriter): a reader who has seen one can read the other.
//
// Strings live in a pool at the front and the payload refers to them by index,
// which is what ByteWriter already does for .map. For a mesh with 40 submeshes
// sharing 3 material names that is the difference between 40 copies and 3.
//
// Endianness is fixed little by ByteStream. There is one target.
namespace eng::content {

// Magic is exactly 8 bytes, not NUL-terminated: "RAVENMSH", "RAVENTEX".
using CookedMagic = char[8];

// Writes header + pool + payload atomically: a temp file next to the
// destination, then a rename. A conditioner killed mid-write therefore leaves
// the previous output intact rather than a truncated one the game would load.
bool writeCookedFile(const std::filesystem::path& path, const char magic[8],
                     uint16_t version, const io::ByteWriter& body,
                     std::string& error);

struct CookedFileBody {
    uint16_t version = 0;
    std::vector<uint8_t> bytes;
    std::vector<std::string> pool;

    // The reader over the payload. The body must outlive it -- ByteReader
    // borrows the pool by reference.
    io::ByteReader reader() const
    {
        return io::ByteReader(bytes.data(), bytes.size(), pool);
    }
};

// Reads and validates magic and pool bounds. `version` is returned rather than
// checked here: which versions a format accepts is that format's business.
bool readCookedFile(const std::filesystem::path& path, const char magic[8],
                    CookedFileBody& out, std::string& error);

// True when the file exists and starts with `magic`. Used by the runtime to
// answer "is there a cooked form of this?" without paying for a full read.
bool cookedFileMatches(const std::filesystem::path& path, const char magic[8]);

} // namespace eng::content
