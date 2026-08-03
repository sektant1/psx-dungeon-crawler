#include <eng/assets/AssetName.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace eng::assets {
namespace {

bool lowerIdSegment(std::string_view text)
{
    if (text.empty() || !std::islower(static_cast<unsigned char>(text.front())))
        return false;
    bool previousUnderscore = false;
    for (char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (c == '_') {
            if (previousUnderscore)
                return false;
            previousUnderscore = true;
            continue;
        }
        if (!std::islower(byte) && !std::isdigit(byte))
            return false;
        previousUnderscore = false;
    }
    return !previousUnderscore;
}

bool pascalSegment(std::string_view text)
{
    if (text.empty() || !std::isupper(static_cast<unsigned char>(text.front())))
        return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0;
    });
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(
            start, end == std::string_view::npos ? std::string_view::npos
                                                  : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return parts;
}

std::optional<AssetNameIssue> pathIssue(std::string_view path,
                                        bool shaderOnly)
{
    if (path.empty())
        return AssetNameIssue{"empty", 0, "name is empty"};
    if (path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find("..") != std::string_view::npos)
        return AssetNameIssue{"path.relative", 0,
                              "path must be relative and use forward slashes"};

    const std::vector<std::string_view> parts = split(path, '/');
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!lowerIdSegment(parts[i]))
            return AssetNameIssue{"path.directory", 0,
                                  "directories use lowercase snake case"};
    }
    const std::string_view file = parts.back();
    const std::size_t dot = file.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == file.size())
        return AssetNameIssue{"path.extension", path.size(),
                              "runtime file needs a lowercase extension"};
    if (!lowerIdSegment(file.substr(0, dot)))
        return AssetNameIssue{"path.filename", path.size() - file.size(),
                              "filename stem uses lowercase snake case"};
    const std::string_view extension = file.substr(dot + 1);
    if (!std::all_of(extension.begin(), extension.end(), [](char c) {
            return std::islower(static_cast<unsigned char>(c)) != 0 ||
                   std::isdigit(static_cast<unsigned char>(c)) != 0;
        }))
        return AssetNameIssue{"path.extension", path.size() - extension.size(),
                              "extension uses lowercase ASCII"};
    if (shaderOnly && extension != "vert" && extension != "frag" &&
        extension != "glsl")
        return AssetNameIssue{"shader.extension", path.size() - extension.size(),
                              "shader extension must be vert, frag, or glsl"};
    return std::nullopt;
}

} // namespace

std::optional<AssetNameIssue> validateAssetName(AssetNameKind kind,
                                                std::string_view name)
{
    if (name.empty())
        return AssetNameIssue{"empty", 0, "name is empty"};
    switch (kind) {
    case AssetNameKind::LocalId:
        if (!lowerIdSegment(name))
            return AssetNameIssue{"id.local", 0,
                                  "id uses lowercase snake case"};
        return std::nullopt;
    case AssetNameKind::QualifiedId: {
        const std::vector<std::string_view> parts = split(name, '.');
        if (parts.size() < 2)
            return AssetNameIssue{"id.namespace", 0,
                                  "qualified id needs at least two segments"};
        for (const std::string_view part : parts)
            if (!lowerIdSegment(part))
                return AssetNameIssue{"id.segment", 0,
                                      "qualified id segments use lowercase snake case"};
        return std::nullopt;
    }
    case AssetNameKind::RuntimePath:
        return pathIssue(name, false);
    case AssetNameKind::ShaderPath:
        return pathIssue(name, true);
    case AssetNameKind::MaterialId: {
        const std::vector<std::string_view> parts = split(name, '/');
        if (parts.size() != 3)
            return AssetNameIssue{"material.shape", 0,
                                  "material id is Owner/Domain/Name"};
        for (const std::string_view part : parts)
            if (!pascalSegment(part))
                return AssetNameIssue{"material.segment", 0,
                                      "material segments use PascalCase"};
        return std::nullopt;
    }
    }
    return std::nullopt;
}

std::string canonicalToken(std::string_view source)
{
    std::string out;
    out.reserve(source.size());
    bool separator = true;
    bool previousLowerOrDigit = false;
    for (char c : source) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (!std::isalnum(byte)) {
            separator = true;
            previousLowerOrDigit = false;
            continue;
        }
        const bool upper = std::isupper(byte) != 0;
        if (!out.empty() && (separator || (upper && previousLowerOrDigit)) &&
            out.back() != '_')
            out.push_back('_');
        out.push_back(char(std::tolower(byte)));
        separator = false;
        previousLowerOrDigit = std::islower(byte) || std::isdigit(byte);
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front())))
        out.insert(0, "asset_");
    return out;
}

std::string friendlyAssetLabel(std::string_view stableName)
{
    const std::size_t slash = stableName.find_last_of('/');
    if (slash != std::string_view::npos)
        stableName.remove_prefix(slash + 1);
    const std::size_t dot = stableName.rfind('.');
    if (dot != std::string_view::npos) {
        const std::string_view suffix = stableName.substr(dot + 1);
        const bool extension = suffix == "png" || suffix == "jpg" ||
                               suffix == "jpeg" || suffix == "obj" ||
                               suffix == "glb" || suffix == "vert" ||
                               suffix == "frag" || suffix == "glsl" ||
                               suffix == "toml" || suffix == "scn";
        stableName = extension ? stableName.substr(0, dot)
                               : stableName.substr(dot + 1);
    }

    std::string out;
    out.reserve(stableName.size() + 4);
    bool newWord = true;
    bool previousLower = false;
    for (char c : stableName) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (c == '_' || c == '-') {
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
            newWord = true;
            previousLower = false;
            continue;
        }
        const bool upperBoundary = std::isupper(byte) && previousLower;
        if (upperBoundary && !out.empty() && out.back() != ' ')
            out.push_back(' ');
        out.push_back(newWord ? char(std::toupper(byte)) : c);
        newWord = false;
        previousLower = std::islower(byte) != 0;
    }
    return out;
}

} // namespace eng::assets
