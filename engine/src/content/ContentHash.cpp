#include <eng/content/ContentHash.h>

#include <array>
#include <cstdio>

namespace eng::content {

Hash hashFile(const std::filesystem::path& path)
{
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file)
        return 0;
    Hash h = kHashBasis;
    std::array<unsigned char, 64 * 1024> buffer{};
    size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0)
        h = hashBytes(buffer.data(), read, h);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed)
        return 0;
    // The empty file legitimately hashes to the basis, and 0 is the failure
    // signal, so the two can never be confused. A real hash that happens to
    // land on 0 would be read as a failure; the odds are 2^-64 and the cost is
    // one extra rebuild, so it is left alone rather than special-cased.
    return h;
}

std::string hashToHex(Hash value)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

bool hashFromHex(std::string_view text, Hash& out)
{
    if (text.size() != 16)
        return false;
    Hash value = 0;
    for (char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<Hash>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<Hash>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<Hash>(c - 'A' + 10);
        else
            return false;
    }
    out = value;
    return true;
}

} // namespace eng::content
