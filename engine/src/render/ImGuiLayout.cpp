#include <eng/render/ImGuiLayout.h>

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <imgui.h>

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

// Default: on. A shipping build flips this one define and the engine stops
// opening the file entirely -- no path resolution, no writes, and DebugTools
// falls back to rebuilding its shipped layout every run.
#ifndef ENG_UI_LAYOUT_PERSISTENCE
#define ENG_UI_LAYOUT_PERSISTENCE 1
#endif

namespace eng::imgui_layout {
namespace {

struct State {
    bool enabled = false;
    bool restored = false;
    std::string path;
};

State& state()
{
    static State s;
    return s;
}

bool isOff(const char* value)
{
    return !std::strcmp(value, "0") || !std::strcmp(value, "off") ||
           !std::strcmp(value, "none") || !std::strcmp(value, "false");
}

std::string slug(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c))
            out += char(std::tolower(c));
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

// Which app is running, for the filename. The executable's own name is the only
// identity available this far down that is both stable across runs and distinct
// between the game, the demo and the editor.
std::string appName()
{
    std::error_code ec;
    const std::filesystem::path exe =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    const std::string name = ec ? std::string() : slug(exe.filename().string());
    return name.empty() ? std::string("app") : name;
}

} // namespace

bool enabled()
{
    return state().enabled;
}
const std::string& path()
{
    return state().path;
}
bool restored()
{
    return state().restored;
}

void install()
{
    State& s = state();
    s = State{};

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

#if ENG_UI_LAYOUT_PERSISTENCE
    // The default lives with the engine's other UI data, so an arrangement can
    // be committed and shipped rather than living in somebody's home directory.
    //
    // packDir(), not resolve(): this is the one caller in the engine that
    // *writes*, and on a first run there is no file for resolve() to find. If
    // the engine pack is not mounted -- a headless or misconfigured start --
    // persistence simply stays off rather than guessing at a path.
    const std::filesystem::path enginePack = assets::packDir("content");
    if (enginePack.empty()) {
        log::warn("ImGuiLayout: the engine pack is not mounted; layout "
                  "persistence is off");
        return;
    }
    s.path = (enginePack / "ui" /
              ("debug_layout_" + appName() + ".ini")).string();
    if (const char* override = std::getenv("RAVEN_UI_LAYOUT")) {
        if (isOff(override)) {
            log::info("ImGuiLayout: persistence off (RAVEN_UI_LAYOUT)");
            s.path.clear();
            return;
        }
        s.path = override;
    }

    std::error_code ec;
    const std::filesystem::path file(s.path);
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        log::warn("ImGuiLayout: cannot create '%s': %s",
                  file.parent_path().string().c_str(), ec.message().c_str());
        s.path.clear();
        return;
    }

    s.restored = std::filesystem::exists(file, ec) && !ec;
    s.enabled = true;
    // imgui keeps the pointer, so the string has to outlive the context. It is
    // a function-local static, which does.
    io.IniFilename = s.path.c_str();
    // imgui only loads the ini lazily on the first NewFrame. Loading it here
    // instead means restored() and the file's contents agree by the time
    // DebugTools decides whether to impose its own layout.
    if (s.restored)
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    log::info("ImGuiLayout: panel layout %s %s",
              s.restored ? "restored from" : "will be saved to",
              s.path.c_str());
#else
    log::info("ImGuiLayout: persistence compiled out");
#endif
}

void save()
{
    if (!state().enabled)
        return;
    ImGui::SaveIniSettingsToDisk(state().path.c_str());
}

} // namespace eng::imgui_layout
