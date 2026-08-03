#include <eng/assets/AssetRoot.h>

#include <eng/Log.h>

#include <cstdlib>
#include <map>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace fs = std::filesystem;

namespace eng::assets {

namespace {

// How the root was found. It is kept because it is the only thing that
// distinguishes an installed layout from a dev tree, and project() has to
// answer differently for the two.
enum class Origin { None, Override, Env, InstallShare, ExeLocal, Dev };

struct State {
    bool ready = false;
    Origin origin = Origin::None;
    fs::path root;
    fs::path project;
    std::vector<Pack> packs;
    std::map<std::string, std::vector<std::string>> mounts;
    std::vector<std::string> internalMaterials;
    std::vector<Pack> mounted;
};

State& state()
{
    static State s;
    return s;
}

const char* kManifest = "assets.toml";

// The directory holding the running executable. Everything but the dev
// fallback is relative to it, which is what makes an installed build possible:
// no absolute source path survives into the binary except RAVEN_ASSET_ROOT_DEV,
// and that one is only consulted last.
fs::path exeDir()
{
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec)
        return {};
    return exe.parent_path();
}

bool hasManifest(const fs::path& dir)
{
    std::error_code ec;
    return !dir.empty() && fs::is_regular_file(dir / kManifest, ec);
}

// D5. First candidate that actually contains a manifest wins; an explicit
// override is not a candidate but *the* candidate, because a tool that names a
// root and silently gets a different one is worse than a tool that fails.
bool discover(const std::string& rootOverride, fs::path& out, Origin& origin)
{
    if (!rootOverride.empty()) {
        out = rootOverride;
        origin = Origin::Override;
        if (hasManifest(out))
            return true;
        log::error("assets: '%s' is not a content root (no %s)",
                   rootOverride.c_str(), kManifest);
        return false;
    }

    if (const char* env = std::getenv("RAVEN_ASSET_ROOT")) {
        if (*env) {
            out = env;
            origin = Origin::Env;
            if (hasManifest(out))
                return true;
            log::error("assets: RAVEN_ASSET_ROOT='%s' has no %s", env, kManifest);
            return false;
        }
    }

    const fs::path exe = exeDir();
    if (!exe.empty()) {
        const fs::path installed =
            exe.parent_path() / "share" / "raven-engine" / "assets";
        if (hasManifest(installed)) {
            out = installed;
            origin = Origin::InstallShare;
            return true;
        }
        const fs::path portable = exe / "assets";
        if (hasManifest(portable)) {
            out = portable;
            origin = Origin::ExeLocal;
            return true;
        }
    }

#ifdef RAVEN_ASSET_ROOT_DEV
    const fs::path dev = RAVEN_ASSET_ROOT_DEV;
    if (hasManifest(dev)) {
        out = dev;
        origin = Origin::Dev;
        return true;
    }
#endif

    log::error("assets: no content root found (RAVEN_ASSET_ROOT, <exe>/../share/"
               "raven-engine/assets, <exe>/assets, then the build default)");
    return false;
}

// The install layout buries the root three levels below the prefix; every
// other layout keeps the root directly inside the project. This is the whole
// of what replaces the assetRoot + "/../.." climb.
fs::path projectFor(const fs::path& root, Origin origin)
{
    if (origin == Origin::InstallShare)
        return root.parent_path().parent_path().parent_path();
    return root.parent_path();
}

const char* originName(Origin origin)
{
    switch (origin) {
    case Origin::Override:
        return "override";
    case Origin::Env:
        return "RAVEN_ASSET_ROOT";
    case Origin::InstallShare:
        return "install";
    case Origin::ExeLocal:
        return "exe-local";
    case Origin::Dev:
        return "dev";
    case Origin::None:
        break;
    }
    return "none";
}

bool readPacks(const toml::table& manifest, const fs::path& root,
               std::vector<Pack>& out)
{
    const toml::array* arr = manifest["pack"].as_array();
    if (!arr || arr->empty()) {
        log::error("assets: %s declares no [[pack]]", kManifest);
        return false;
    }

    for (const toml::node& node : *arr) {
        const toml::table* tbl = node.as_table();
        if (!tbl) {
            log::error("assets: a [[pack]] entry is not a table");
            return false;
        }

        Pack pack;
        if (const toml::value<std::string>* id = (*tbl)["id"].as_string())
            pack.id = id->get();
        if (pack.id.empty()) {
            log::error("assets: a [[pack]] entry has no id");
            return false;
        }
        for (const Pack& seen : out) {
            if (seen.id == pack.id) {
                log::error("assets: duplicate pack id '%s'", pack.id.c_str());
                return false;
            }
        }

        std::string dir;
        if (const toml::value<std::string>* d = (*tbl)["dir"].as_string())
            dir = d->get();
        if (dir.empty()) {
            log::error("assets: pack '%s' has no dir", pack.id.c_str());
            return false;
        }

        // Normalised so the P0 manifest's "../assets/engine" hops -- which
        // disappear in P1/P2 -- do not leak into every logged path.
        std::error_code ec;
        pack.dir = fs::weakly_canonical(root / dir, ec);
        if (ec)
            pack.dir = (root / dir).lexically_normal();
        if (!fs::is_directory(pack.dir, ec)) {
            log::error("assets: pack '%s' dir '%s' does not exist",
                       pack.id.c_str(), pack.dir.c_str());
            return false;
        }

        if (const toml::array* res = (*tbl)["resources"].as_array()) {
            for (const toml::node& entry : *res) {
                if (const toml::value<std::string>* s = entry.as_string())
                    pack.resources.push_back(s->get());
                else {
                    log::error("assets: pack '%s' has a non-string resources "
                               "entry",
                               pack.id.c_str());
                    return false;
                }
            }
        }

        out.push_back(std::move(pack));
    }
    return true;
}

// Mount sets are not validated here on purpose: a set naming a pack that does
// not exist is only a problem for the app that mounts it, and failing init
// would take down every other app over content it never asked for.
bool readMounts(const toml::table& manifest,
                std::map<std::string, std::vector<std::string>>& out)
{
    const toml::table* tbl = manifest["mounts"].as_table();
    if (!tbl) {
        log::error("assets: %s declares no [mounts]", kManifest);
        return false;
    }

    for (auto&& [key, value] : *tbl) {
        const toml::array* ids = value.as_array();
        if (!ids) {
            log::error("assets: mount set '%s' is not an array",
                       std::string(key.str()).c_str());
            return false;
        }
        std::vector<std::string>& set = out[std::string(key.str())];
        for (const toml::node& entry : *ids) {
            if (const toml::value<std::string>* s = entry.as_string())
                set.push_back(s->get());
            else {
                log::error("assets: mount set '%s' has a non-string entry",
                           std::string(key.str()).c_str());
                return false;
            }
        }
    }
    return true;
}

// [materials] is optional -- a manifest that hides nothing is a valid
// manifest -- but a malformed one is not, because the failure mode of a
// silently dropped entry is an internal material appearing in a picker where
// someone can assign it to the level.
bool readMaterials(const toml::table& manifest, std::vector<std::string>& out)
{
    const toml::table* tbl = manifest["materials"].as_table();
    if (!tbl)
        return true;

    const toml::node_view<const toml::node> node = (*tbl)["internal"];
    if (!node)
        return true;

    const toml::array* names = node.as_array();
    if (!names) {
        log::error("assets: [materials] internal is not an array");
        return false;
    }
    for (const toml::node& entry : *names) {
        const toml::value<std::string>* s = entry.as_string();
        if (!s) {
            log::error("assets: [materials] internal has a non-string entry");
            return false;
        }
        out.push_back(s->get());
    }
    return true;
}

const Pack* findPack(const std::vector<Pack>& packs, std::string_view id)
{
    for (const Pack& pack : packs) {
        if (pack.id == id)
            return &pack;
    }
    return nullptr;
}

// A logical path is pack-relative by construction: absolute paths and ".."
// components are rejected rather than normalised away, so no caller can reach
// out of the content root by way of a data file.
bool sanitise(std::string_view logical, fs::path& out)
{
    if (logical.empty())
        return false;

    fs::path rel(logical);
    if (rel.is_absolute())
        return false;

    fs::path clean;
    for (const fs::path& part : rel) {
        if (part == ".." || part.empty())
            return false;
        if (part == ".")
            continue;
        clean /= part;
    }
    if (clean.empty())
        return false;

    out = clean;
    return true;
}

} // namespace

bool init(const std::string& rootOverride)
{
    State& s = state();
    s = State{};

    fs::path found;
    Origin origin = Origin::None;
    if (!discover(rootOverride, found, origin))
        return false;

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(found, ec);
    if (ec)
        canonical = found.lexically_normal();

    const fs::path manifestPath = canonical / kManifest;
    toml::parse_result parsed = toml::parse_file(manifestPath.string());
    if (!parsed) {
        log::error("assets: failed to parse %s: %s", manifestPath.c_str(),
                   std::string(parsed.error().description()).c_str());
        return false;
    }

    std::vector<Pack> packs;
    std::map<std::string, std::vector<std::string>> mounts;
    std::vector<std::string> internalMaterials;
    if (!readPacks(parsed.table(), canonical, packs) ||
        !readMounts(parsed.table(), mounts) ||
        !readMaterials(parsed.table(), internalMaterials))
        return false;

    s.root = canonical;
    s.project = projectFor(canonical, origin);
    s.origin = origin;
    s.packs = std::move(packs);
    s.mounts = std::move(mounts);
    s.internalMaterials = std::move(internalMaterials);
    s.ready = true;

    log::info("assets: root %s (%s), %zu packs", s.root.c_str(),
              originName(origin), s.packs.size());
    return true;
}

bool ready()
{
    return state().ready;
}

const fs::path& root()
{
    return state().root;
}

const fs::path& project()
{
    return state().project;
}

bool mount(const std::string& mountSet)
{
    State& s = state();
    s.mounted.clear();

    if (!s.ready) {
        log::error("assets: mount('%s') before init()", mountSet.c_str());
        return false;
    }

    const auto it = s.mounts.find(mountSet);
    if (it == s.mounts.end()) {
        log::error("assets: unknown mount set '%s'", mountSet.c_str());
        return false;
    }

    std::vector<Pack> resolved;
    for (const std::string& id : it->second) {
        const Pack* pack = findPack(s.packs, id);
        if (!pack) {
            log::error("assets: mount set '%s' names undeclared pack '%s'",
                       mountSet.c_str(), id.c_str());
            return false;
        }
        resolved.push_back(*pack);
    }

    s.mounted = std::move(resolved);
    return true;
}

const std::vector<Pack>& mounted()
{
    return state().mounted;
}

const std::vector<Pack>& packs()
{
    return state().packs;
}

fs::path resolve(std::string_view logical)
{
    const State& s = state();

    fs::path rel;
    if (!s.ready || s.mounted.empty() || !sanitise(logical, rel))
        return {};

    // A pack-qualified path pins one pack and stops there: falling through
    // would make "engine/x" mean "engine's x, or anybody's x", which is the
    // ambiguity the qualifier exists to remove.
    const std::string first = rel.begin()->string();
    if (const Pack* pinned = findPack(s.mounted, first)) {
        fs::path tail;
        for (auto it = ++rel.begin(); it != rel.end(); ++it)
            tail /= *it;
        if (tail.empty())
            return {};
        std::error_code ec;
        const fs::path candidate = pinned->dir / tail;
        return fs::exists(candidate, ec) ? candidate : fs::path{};
    }

    for (const Pack& pack : s.mounted) {
        std::error_code ec;
        const fs::path candidate = pack.dir / rel;
        if (fs::exists(candidate, ec))
            return candidate;
    }
    return {};
}

bool exists(std::string_view logical)
{
    return !resolve(logical).empty();
}

fs::path packDir(std::string_view id)
{
    const Pack* pack = findPack(state().mounted, id);
    return pack ? pack->dir : fs::path{};
}

const std::vector<std::string>& internalMaterials()
{
    return state().internalMaterials;
}

bool materialInternal(std::string_view name)
{
    for (const std::string& entry : state().internalMaterials) {
        if (entry == name)
            return true;
    }
    return false;
}

std::vector<fs::path> resourceDirs()
{
    std::vector<fs::path> dirs;
    for (const Pack& pack : state().mounted) {
        for (const std::string& entry : pack.resources) {
            const fs::path dir = pack.dir / entry;
            std::error_code ec;
            if (fs::is_directory(dir, ec))
                dirs.push_back(dir);
            else
                log::warn("assets: pack '%s' declares missing resource dir "
                          "'%s'",
                          pack.id.c_str(), entry.c_str());
        }
    }
    return dirs;
}

} // namespace eng::assets
