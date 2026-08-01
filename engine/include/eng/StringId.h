#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace eng {

// Hashed string ids: the descriptiveness of a name with the comparison cost of
// an integer (Game Engine Architecture, 4th ed., 6.4.3).
//
// This engine names things with strings everywhere -- material ids, event names,
// profile bins, config keys, weapon ids. A std::string key costs a heap
// allocation to build, a memcmp to compare, and a pointer chase to hash. A
// StringId is one 64-bit integer: it compares in a cycle, fits in a register,
// and can be a `case` label because the hash is constexpr.
//
//   constexpr StringId kFire = "fire_wand"_sid;     // hashed at compile time
//   StringId id = intern(tomlKey);                  // hashed once, at load
//   if (id == kFire) ...                            // one integer compare
//
// 64-bit, not 32: the book notes Naughty Dog moved to a 64-bit hash for The Last
// of Us Part II precisely because a 32-bit space starts colliding at content
// scale. At 64 bits a collision is not something worth designing around --
// intern() still reports one if it ever happens, because a silent id collision
// is the kind of bug that costs a week.
//
// Debug names: only strings that pass through intern() can be recovered with
// c_str(). A compile-time "x"_sid is just a number -- that is the whole point --
// so a system that wants its literals printable interns them once at start-up.
using StringIdValue = std::uint64_t;

// FNV-1a, 64-bit. constexpr so a literal hashes at compile time and costs
// nothing at runtime.
constexpr StringIdValue hashString(const char* s, std::size_t n)
{
    StringIdValue h = 14695981039346656037ull; // FNV offset basis
    for (std::size_t i = 0; i < n; ++i) {
        h ^= StringIdValue(static_cast<unsigned char>(s[i]));
        h *= 1099511628211ull; // FNV prime
    }
    return h;
}

constexpr StringIdValue hashString(std::string_view s)
{
    return hashString(s.data(), s.size());
}

class StringId
{
public:
    constexpr StringId() = default;
    constexpr explicit StringId(StringIdValue v) : mValue(v) {}

    constexpr StringIdValue value() const { return mValue; }
    // The empty string hashes to the FNV basis, so 0 is free to mean "unset".
    constexpr bool valid() const { return mValue != 0; }
    constexpr explicit operator bool() const { return valid(); }

    constexpr bool operator==(StringId o) const { return mValue == o.mValue; }
    constexpr bool operator!=(StringId o) const { return mValue != o.mValue; }
    constexpr bool operator<(StringId o) const { return mValue < o.mValue; }

    // The original text if this id was interned, else a stable "#<hex>" form so
    // a log line or a debug panel never prints an empty cell.
    const char* c_str() const;

private:
    StringIdValue mValue = 0;
};

constexpr StringId operator""_sid(const char* s, std::size_t n)
{
    return StringId(hashString(s, n));
}

// Hash `s` and record it in the debug table, so c_str() can recover it. Safe to
// call repeatedly with the same text (the second call only looks it up), and
// safe from any thread. Interning is the *slow* path by design: hash once at
// load time and keep the id, rather than re-interning per frame.
StringId intern(std::string_view s);

// Text for an id, or nullptr when it was never interned. c_str() is the
// forgiving version of this.
const char* stringFromId(StringId id);

// How many distinct strings the table holds. For the memory/debug panel.
std::size_t internedCount();

// Collisions detected so far: two different strings that interned to one id.
// Always zero in practice at 64 bits; a non-zero reading means the content
// pipeline just produced two names the engine can no longer tell apart, and it
// is logged as an error the moment it happens.
std::size_t stringIdCollisions();

} // namespace eng

template <> struct std::hash<eng::StringId>
{
    std::size_t operator()(eng::StringId id) const noexcept
    {
        // Already a well-mixed 64-bit hash; re-hashing only costs cycles.
        return std::size_t(id.value());
    }
};
