#pragma once
#include <string>

namespace ed {

// Editor preferences that outlive a session.
//
// Everything else the editor remembers is either the document (a .scn), the
// window layout (ImGui's own ini) or a convenience list (recent.txt). This is
// the third kind: choices about how the editor *behaves*, which an author makes
// once and should never make twice.
//
// Deliberately small, and meant to stay that way for a while: one group, the
// one the editor cannot be trusted without. A settings screen that opens onto
// forty toggles is a settings screen nobody reads, and every toggle in it is a
// branch somebody has to keep working.
//
// Plain `key = value` text, not TOML: the file is written by the editor and
// read by the editor, an unknown key is skipped rather than diagnosed, and a
// missing file is not an error -- it is a first run. The shipped
// config/editor.toml stays what it is, the *authored* configuration (window
// size, key bindings) that belongs in version control; this is per-user state
// and lives beside recent.txt under artifacts/.
struct EditorSettings {
    // --- autosave --------------------------------------------------------
    // The one setting this screen exists for. The editor writes a .autosave.scn
    // beside the scene while the document is dirty; Scene > Recover autosave
    // reads it back. Off is a real choice -- a scene on a network share, a
    // machine where the write stutters -- and it has to be visible rather than
    // a constant somebody edits in C++.
    bool autosaveEnabled = true;
    // Seconds between backups, counted only while there is unsaved work.
    // Clamped by sanitised(): a zero here would write the whole document every
    // frame, and an hour is not a backup.
    float autosaveSeconds = 120.0f;

    // --- the viewport ----------------------------------------------------
    // What the editor draws with. These were session-only toggles scattered
    // across the View menu, which meant every launch started with the render
    // profile, the lighting and the marks back at their defaults -- and an
    // author who works under one profile had to re-pick it every morning.
    //
    // The preset is stored by NAME, not by the id the engine uses: an id is an
    // index into a table that grows, and a settings file that says "7" means
    // whatever is seventh today. Empty is "the engine's default", which is also
    // what an unknown name falls back to.
    std::string viewportPreset;
    bool gameLighting = false; // the level's own palette, not the flat editor key
    bool entityMarks = true;
    bool volumeMarks = true;
    bool frameStats = true;
    bool grid = true;
    // Tool chrome and text scale together. Kept independent of framebuffer DPI:
    // this is the author's readability preference, especially useful on TVs
    // and high-resolution monitors viewed at a distance.
    float uiScale = 1.0f;

    // --- playtest --------------------------------------------------------
    // What F5 launches the game with. The editor spawns the game as a separate
    // process, so every one of these is an environment variable the game
    // already understands -- nothing here is a second implementation of a
    // switch the game has.
    //
    // Matching the viewport by default is the point: per-entity ShaderParams
    // (a rim light, a tint, an opacity) read completely differently under
    // `ps1` and under `dungeon`, so tuning one in the editor and playing under
    // another is tuning blind. Unticking it is for the deliberate comparison.
    bool playtestMatchesViewport = true;
    std::string playtestPreset; // used when it does not; empty = engine default
    bool playFromCamera = true; // PSX_PLAY_FROM, from the viewport camera
    bool playtestConsole = false;   // PSX_DEBUG_UI: open the debug console
    bool playtestColliders = false; // PSX_SHOW_COLLIDERS: physics shapes on
    bool playtestFullscreen = false; // PSX_FULLSCREEN

    static constexpr float kMinSeconds = 15.0f;
    static constexpr float kMaxSeconds = 900.0f;
    static constexpr float kMinUiScale = 0.80f;
    static constexpr float kMaxUiScale = 2.00f;
};

// The same settings with every value forced into range. Applied on load, so a
// hand-edited or truncated file cannot put the editor into a state its own UI
// could not have produced.
EditorSettings sanitised(EditorSettings settings);

// Reads `file`. A missing or unreadable file yields the defaults, because a
// first run is the common case and never a failure.
EditorSettings loadEditorSettings(const std::string& file);
// Writes `file`, creating its directory. False on a write error; the caller
// decides whether that is worth a line in the status bar.
bool saveEditorSettings(const std::string& file, const EditorSettings& settings);

// One step of the autosave clock, as a pure function of the settings and what
// the document is doing.
//
// Split out so the rule is testable without an ImGui frame, a renderer or a
// filesystem -- the alternative is a timer that only ever gets exercised by
// waiting two minutes and hoping.
struct AutosaveTick {
    float remaining = 0.0f; // the new countdown
    bool write = false;     // back up now
};
AutosaveTick stepAutosave(const EditorSettings& settings, float remaining,
                          float dt, bool documentDirty);

} // namespace ed
