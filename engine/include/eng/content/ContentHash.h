#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Content hashing for the asset conditioning pipeline.
//
// The pipeline decides whether to rebuild an asset by comparing a *build key*
// against the one recorded the last time it built: the source bytes, the import
// settings, and the conditioner's own version, folded into one 64-bit value.
// Timestamps are deliberately not part of it -- a git checkout rewrites every
// mtime, and a pipeline that rebuilds the whole tree after a branch switch is a
// pipeline nobody runs.
//
// FNV-1a, the same function eng::StringId uses, for the same reason: it is
// three lines, has no dependencies, and at 64 bits an accidental collision
// across a content tree is not a risk worth a library over. This is not a
// security boundary -- a hostile asset that forces a stale build is a problem
// this engine does not have.
namespace eng::content {

using Hash = uint64_t;

constexpr Hash kHashBasis = 14695981039346656037ull;

constexpr Hash hashBytes(const void* data, size_t size, Hash seed = kHashBasis)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    Hash h = seed;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<Hash>(bytes[i]);
        h *= 1099511628211ull;
    }
    return h;
}

constexpr Hash hashText(std::string_view text, Hash seed = kHashBasis)
{
    Hash h = seed;
    for (char c : text) {
        h ^= static_cast<Hash>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;
    }
    return h;
}

// Mixes a fixed-width value in without going through a string. Used for
// version numbers and enum fields in a build key.
constexpr Hash hashValue(uint64_t value, Hash seed = kHashBasis)
{
    return hashBytes(&value, sizeof(value), seed);
}

// The file's bytes, streamed so a 200 MB source model does not have to be
// resident. Returns 0 -- never a valid content hash, because the empty file
// hashes to the basis -- when the file cannot be read, which the caller must
// treat as "cannot build", not as "unchanged".
Hash hashFile(const std::filesystem::path& path);

// 16 lowercase hex digits, and back. This is the on-disk spelling in .meta
// sidecars, the build cache and the manifest, so it is fixed-width on purpose:
// a column of ids stays a column.
std::string hashToHex(Hash value);
bool hashFromHex(std::string_view text, Hash& out);

} // namespace eng::content
