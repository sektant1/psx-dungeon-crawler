#include <editor/project/EditorSettings.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace ed {

namespace {

// "key = value", with whatever spacing a human left behind. Returns false for
// a blank line or a comment, which are both legal and both nothing to do.
bool parseLine(const std::string& line, std::string& key, std::string& value)
{
    const std::size_t hash = line.find('#');
    const std::string body = line.substr(0, hash);
    const std::size_t equals = body.find('=');
    if (equals == std::string::npos)
        return false;

    const auto trim = [](std::string text) {
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return std::string{};
        const std::size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    };
    key = trim(body.substr(0, equals));
    value = trim(body.substr(equals + 1));
    return !key.empty();
}

bool asBool(const std::string& value, bool fallback)
{
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    return fallback;
}

float asFloat(const std::string& value, float fallback)
{
    try {
        const float parsed = std::stof(value);
        return std::isfinite(parsed) ? parsed : fallback;
    } catch (...) {
        // A garbled number is a garbled number; the default is a better answer
        // than refusing to start.
        return fallback;
    }
}

} // namespace

EditorSettings sanitised(EditorSettings settings)
{
    if (!std::isfinite(settings.autosaveSeconds))
        settings.autosaveSeconds = EditorSettings{}.autosaveSeconds;
    settings.autosaveSeconds =
        std::clamp(settings.autosaveSeconds, EditorSettings::kMinSeconds,
                   EditorSettings::kMaxSeconds);
    if (!std::isfinite(settings.uiScale))
        settings.uiScale = EditorSettings{}.uiScale;
    settings.uiScale = std::clamp(settings.uiScale, EditorSettings::kMinUiScale,
                                  EditorSettings::kMaxUiScale);
    // An empty theme is a truncated file, not a request for imgui's default.
    if (settings.theme.empty())
        settings.theme = EditorSettings{}.theme;
    return settings;
}

EditorSettings loadEditorSettings(const std::string& file)
{
    EditorSettings settings;
    std::ifstream in(file);
    if (!in)
        return settings; // first run

    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (!parseLine(line, key, value))
            continue;
        if (key == "autosave.enabled")
            settings.autosaveEnabled = asBool(value, settings.autosaveEnabled);
        else if (key == "autosave.interval_seconds")
            settings.autosaveSeconds = asFloat(value, settings.autosaveSeconds);
        else if (key == "viewport.preset")
            settings.viewportPreset = value;
        else if (key == "viewport.game_lighting")
            settings.gameLighting = asBool(value, settings.gameLighting);
        else if (key == "viewport.entity_marks")
            settings.entityMarks = asBool(value, settings.entityMarks);
        else if (key == "viewport.volume_marks")
            settings.volumeMarks = asBool(value, settings.volumeMarks);
        else if (key == "viewport.frame_stats")
            settings.frameStats = asBool(value, settings.frameStats);
        else if (key == "viewport.grid")
            settings.grid = asBool(value, settings.grid);
        else if (key == "ui.scale")
            settings.uiScale = asFloat(value, settings.uiScale);
        else if (key == "ui.theme")
            settings.theme = value;
        else if (key == "playtest.match_viewport")
            settings.playtestMatchesViewport =
                asBool(value, settings.playtestMatchesViewport);
        else if (key == "playtest.preset")
            settings.playtestPreset = value;
        else if (key == "playtest.play_from_camera")
            settings.playFromCamera = asBool(value, settings.playFromCamera);
        else if (key == "playtest.console")
            settings.playtestConsole = asBool(value, settings.playtestConsole);
        else if (key == "playtest.colliders")
            settings.playtestColliders = asBool(value, settings.playtestColliders);
        else if (key == "playtest.fullscreen")
            settings.playtestFullscreen =
                asBool(value, settings.playtestFullscreen);
        // Anything else: skipped. A settings file written by a newer editor has
        // to open in an older one, and a key that was removed must not be a
        // reason to refuse the rest of the file.
    }
    return sanitised(settings);
}

bool saveEditorSettings(const std::string& file, const EditorSettings& settings)
{
    std::error_code ec;
    const std::filesystem::path path(file);
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    if (!out)
        return false;

    // Commented, because the file is in a place people look at (artifacts/) and
    // a bare `autosave.interval_seconds = 120` is not self-explanatory when the
    // editor is not in front of you.
    out << "# Scene editor preferences. Written by Edit > Settings.\n";
    out << "autosave.enabled = " << (settings.autosaveEnabled ? "true" : "false")
        << '\n';
    out << "# seconds between backups, counted only while there is unsaved work\n";
    out << "autosave.interval_seconds = " << settings.autosaveSeconds << '\n';

    out << "\n# the editor viewport. The preset is a name, not an id: an id is\n";
    out << "# an index into a table that grows.\n";
    out << "viewport.preset = " << settings.viewportPreset << '\n';
    out << "viewport.game_lighting = "
        << (settings.gameLighting ? "true" : "false") << '\n';
    out << "viewport.entity_marks = "
        << (settings.entityMarks ? "true" : "false") << '\n';
    out << "viewport.volume_marks = "
        << (settings.volumeMarks ? "true" : "false") << '\n';
    out << "viewport.frame_stats = "
        << (settings.frameStats ? "true" : "false") << '\n';
    out << "viewport.grid = " << (settings.grid ? "true" : "false") << '\n';
    out << "ui.scale = " << settings.uiScale << '\n';
    out << "ui.theme = " << settings.theme << '\n';

    out << "\n# what F5 launches the game with. Each one is an environment\n";
    out << "# variable the game already understands.\n";
    out << "playtest.match_viewport = "
        << (settings.playtestMatchesViewport ? "true" : "false") << '\n';
    out << "playtest.preset = " << settings.playtestPreset << '\n';
    out << "playtest.play_from_camera = "
        << (settings.playFromCamera ? "true" : "false") << '\n';
    out << "playtest.console = " << (settings.playtestConsole ? "true" : "false")
        << '\n';
    out << "playtest.colliders = "
        << (settings.playtestColliders ? "true" : "false") << '\n';
    out << "playtest.fullscreen = "
        << (settings.playtestFullscreen ? "true" : "false") << '\n';
    out.flush();
    const bool wrote = bool(out);
    out.close();
    if (!wrote) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

AutosaveTick stepAutosave(const EditorSettings& settings, float remaining,
                          float dt, bool documentDirty)
{
    AutosaveTick tick;
    // The clock only runs while there is something to lose, and it resets when
    // the document is saved: a backup of a file identical to the one on disk is
    // a write for nothing, and it would keep the "a newer autosave exists"
    // prompt permanently armed.
    if (!settings.autosaveEnabled || !documentDirty) {
        tick.remaining = settings.autosaveSeconds;
        return tick;
    }
    tick.remaining = remaining - dt;
    if (tick.remaining > 0.0f)
        return tick;
    // Reset rather than accumulate the overshoot: a frame that took a second
    // (a cook, a material reload) must not queue up a second backup behind it.
    tick.remaining = settings.autosaveSeconds;
    tick.write = true;
    return tick;
}

} // namespace ed
