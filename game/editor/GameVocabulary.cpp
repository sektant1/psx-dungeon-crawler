#include "GameVocabulary.h"

#include <algorithm>
#include <fstream>

namespace ed {
namespace {

// The id in a `[section.id]` header, or empty when the line is not one.
//
// Deliberately strict: a header with a further dot (`[enemy.hollow.stats]`) is
// a sub-table of an enemy, not another enemy, and treating it as one would fill
// the drop-down with "hollow.stats".
std::string headerId(std::string line, const std::string& section)
{
    // Trim, then require the exact bracket form. Anything else -- a key, a
    // comment, an array-of-tables `[[...]]` -- is not a table header.
    const std::size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    line = line.substr(begin);
    if (line.empty() || line.front() != '[' || line.size() < 2 ||
        line[1] == '[')
        return {};
    const std::size_t close = line.find(']');
    if (close == std::string::npos)
        return {};

    const std::string inside = line.substr(1, close - 1);
    const std::string prefix = section + ".";
    if (inside.rfind(prefix, 0) != 0)
        return {};
    const std::string id = inside.substr(prefix.size());
    if (id.empty() || id.find('.') != std::string::npos)
        return {};
    return id;
}

} // namespace

std::vector<std::string> tomlSectionIds(const std::string& path,
                                        const std::string& section)
{
    std::vector<std::string> ids;
    std::ifstream in(path);
    if (!in)
        return ids; // a missing table leaves the field free text, not broken

    std::string line;
    while (std::getline(in, line)) {
        std::string id = headerId(line, section);
        if (!id.empty())
            ids.push_back(std::move(id));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<std::string> enemyIdsFromToml(const std::string& enemiesToml)
{
    return tomlSectionIds(enemiesToml, "enemy");
}

} // namespace ed
