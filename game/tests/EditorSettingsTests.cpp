// Editor preferences and the autosave clock.
//
// The clock is the reason this file exists. It is the one piece of the editor
// whose failure is invisible until it matters -- a backup that silently stopped
// being written looks exactly like one that is working -- and testing it by
// waiting two minutes with an editor open is not testing it.

#include "EditorSettings.h"
#include "RunGame.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorSettingsTests: " << message << '\n';
        std::exit(1);
    }
}

static std::string tempFile(const char* name)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "psx-editor-settings-tests";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / name).string();
}

int main()
{
    // --- defaults, and a first run -----------------------------------------
    {
        const EditorSettings defaults;
        require(defaults.autosaveEnabled,
                "autosave is on unless somebody turns it off");
        require(defaults.autosaveSeconds >= EditorSettings::kMinSeconds &&
                    defaults.autosaveSeconds <= EditorSettings::kMaxSeconds,
                "the default interval is inside its own range");

        const EditorSettings missing =
            loadEditorSettings(tempFile("does-not-exist.txt"));
        require(missing.autosaveEnabled == defaults.autosaveEnabled &&
                    missing.autosaveSeconds == defaults.autosaveSeconds,
                "a missing file is a first run, not a failure");
    }

    // --- round trip ---------------------------------------------------------
    {
        const std::string path = tempFile("round-trip.txt");
        EditorSettings out;
        out.autosaveEnabled = false;
        out.autosaveSeconds = 45.0f;
        out.viewportPreset = "ps1";
        out.gameLighting = true;
        out.entityMarks = false;
        out.volumeMarks = false;
        out.frameStats = false;
        out.playtestMatchesViewport = false;
        out.playtestPreset = "dungeon";
        out.playFromCamera = false;
        out.playtestConsole = true;
        out.playtestColliders = true;
        out.playtestFullscreen = true;
        require(saveEditorSettings(path, out), "the file is written");

        const EditorSettings back = loadEditorSettings(path);
        require(!back.autosaveEnabled, "the toggle survives");
        require(back.autosaveSeconds == 45.0f, "the interval survives");
        require(back.viewportPreset == "ps1" && back.gameLighting &&
                    !back.entityMarks && !back.volumeMarks && !back.frameStats,
                "the viewport group survives");
        require(!back.playtestMatchesViewport &&
                    back.playtestPreset == "dungeon" && !back.playFromCamera &&
                    back.playtestConsole && back.playtestColliders &&
                    back.playtestFullscreen,
                "the playtest group survives");
    }

    // --- what F5 actually launches with -------------------------------------
    // The switches are invisible once the game is running, so the mapping from
    // settings to environment is the part worth pinning down.
    {
        require(playtestEnvironment(PlaytestEnvironment{}).empty(),
                "nothing asked for, nothing exported -- the game's own "
                "defaults are the defaults");

        PlaytestEnvironment options;
        options.renderPreset = "ps1";
        options.playFrom = "1.0,2.0,3.0";
        options.console = true;
        const std::vector<std::string> env = playtestEnvironment(options);
        const auto has = [&env](const std::string& entry) {
            return std::find(env.begin(), env.end(), entry) != env.end();
        };
        require(has("PSX_RENDER_PRESET=ps1"), "the profile is passed by name");
        require(has("PSX_PLAY_FROM=1.0,2.0,3.0"), "and the start position");
        require(has("PSX_DEBUG_UI=1"),
                "a flag the game tests for presence is exported as 1, never "
                "as 0 -- PSX_DEBUG_UI=0 would read as ON");
        require(env.size() == 3, "and nothing else is exported");

        options.console = false;
        const std::vector<std::string> without = playtestEnvironment(options);
        require(std::find(without.begin(), without.end(), "PSX_DEBUG_UI=0") ==
                    without.end(),
                "turning it off omits the variable rather than setting it");
    }

    // --- a hand-edited file cannot break the editor -------------------------
    // The file is plain text in artifacts/, so somebody will edit it.
    {
        const std::string path = tempFile("hand-edited.txt");
        {
            std::ofstream out(path, std::ios::trunc);
            out << "# a comment\n";
            out << "\n";
            out << "   autosave.enabled   =   1   \n";  // spacing, and 1 for true
            out << "autosave.interval_seconds = 0\n";   // below the floor
            out << "autosave.future_setting = 7\n";     // from a newer editor
            out << "not a setting at all\n";
        }
        const EditorSettings loaded = loadEditorSettings(path);
        require(loaded.autosaveEnabled, "'1' reads as true, spacing and all");
        require(loaded.autosaveSeconds == EditorSettings::kMinSeconds,
                "an interval below the floor is clamped, not obeyed -- zero "
                "would write the whole document every frame");
        // An unknown key and a junk line are both skipped rather than fatal: a
        // settings file written by a newer editor has to open in an older one.
    }
    {
        const std::string path = tempFile("garbled.txt");
        {
            std::ofstream out(path, std::ios::trunc);
            out << "autosave.interval_seconds = banana\n";
        }
        const EditorSettings loaded = loadEditorSettings(path);
        require(loaded.autosaveSeconds == EditorSettings{}.autosaveSeconds,
                "an unparseable number falls back to the default");
    }
    {
        EditorSettings absurd;
        absurd.autosaveSeconds = 1e9f;
        require(sanitised(absurd).autosaveSeconds == EditorSettings::kMaxSeconds,
                "and an hour and a half is not a backup either");
    }

    // --- the clock ----------------------------------------------------------
    {
        EditorSettings settings;
        settings.autosaveSeconds = 10.0f;

        // A clean document does not arm it: backing up a file identical to the
        // one on disk is a write for nothing, and it would leave the "a newer
        // autosave exists" prompt permanently armed.
        AutosaveTick tick = stepAutosave(settings, 3.0f, 1.0f, /*dirty=*/false);
        require(!tick.write, "a clean document is never backed up");
        require(tick.remaining == 10.0f, "and the clock is re-armed");

        // A dirty one counts down and fires exactly once.
        float remaining = settings.autosaveSeconds;
        int writes = 0;
        for (int frame = 0; frame < 60; ++frame) { // 60 x 0.25s = 15 seconds
            tick = stepAutosave(settings, remaining, 0.25f, /*dirty=*/true);
            remaining = tick.remaining;
            writes += tick.write ? 1 : 0;
        }
        require(writes == 1,
                "ten seconds of unsaved work in fifteen is one backup");

        // A frame that took longer than the whole interval (a cook, a material
        // reload) must not queue a second backup behind it.
        tick = stepAutosave(settings, 10.0f, 45.0f, /*dirty=*/true);
        require(tick.write, "the overdue backup happens");
        require(tick.remaining == settings.autosaveSeconds,
                "and the clock resets rather than accumulating the overshoot");

        // Off is off, however long the document has been dirty.
        settings.autosaveEnabled = false;
        tick = stepAutosave(settings, 0.0f, 100.0f, /*dirty=*/true);
        require(!tick.write, "disabled means disabled");
    }

    std::cout << "EditorSettingsTests: ok\n";
    return 0;
}
