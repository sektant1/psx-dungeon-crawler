#pragma once

#include <eng/Log.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

// Developer console: a dockable imgui window with a filtered log view and a
// command line. Shared by every app on this engine (game, scene_editor,
// psx_demo) because none of it is game policy -- it is log capture, text
// filtering and a string->callback table.
//
// It is deliberately NOT eng::DebugTools: that panel tunes subsystems with
// sliders and needs a live pointer to each of them. This one only needs text,
// so it survives level rebuilds, works before a scene exists, and is the only
// debug surface that still functions when the thing being debugged is the
// scene build itself.
//
//   DebugConsole console;
//   console.captureEngineLog();                       // once, at startup
//   console.registerCommand("quit", "close the app", [&](const Args& a) {
//       engine.requestClose();
//   });
//   console.bindFloat("r.fov", &fov, 30.0f, 120.0f, "camera field of view");
//   ...
//   console.draw();                                   // inside the imgui frame
//
// Log capture is thread-safe (lines land in a queue and are drained on the UI
// thread); everything else expects the UI thread.
class DebugConsole {
public:
    // Tokenized command line. args[0] is the command name itself, so a handler
    // can report usage under the name it was invoked with.
    using Args = std::vector<std::string>;
    using Handler = std::function<void(const Args&)>;
    // Returns the candidate completions for a partially typed argument. Empty
    // (the default) means the argument does not complete.
    using Completer = std::function<std::vector<std::string>(const Args&)>;

    DebugConsole();
    ~DebugConsole();
    DebugConsole(DebugConsole&&) noexcept;
    DebugConsole& operator=(DebugConsole&&) noexcept;

    // --- log ------------------------------------------------------------
    // Mirrors eng::log output into this console for as long as it lives. Call
    // once; a second call is a no-op.
    void captureEngineLog();
    // Prints a line the app produced itself. `category` groups lines for the
    // category filter ("render", "physics", ...); empty means uncategorised.
    void print(log::Level level, std::string category, std::string text);
    void printf(log::Level level, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));
    void clear();
    // Lines retained before the oldest are dropped. Defaults to 4096.
    void setCapacity(std::size_t lines);

    // --- commands -------------------------------------------------------
    // `help` is shown by the built-in help command and in the completion
    // popup. Re-registering a name replaces it.
    void registerCommand(std::string name, std::string help, Handler fn,
                         Completer complete = {});
    // Convenience bindings: each creates a command that prints the value with
    // no argument and assigns it with one. This is what makes the console a
    // tuning surface without every system writing an ImGui panel. The pointer
    // must outlive the console.
    void bindBool(std::string name, bool* value, std::string help = {});
    void bindInt(std::string name, int* value, int lo, int hi,
                 std::string help = {});
    void bindFloat(std::string name, float* value, float lo, float hi,
                   std::string help = {});

    // Runs a command line as if it had been typed (echoes it, records it in
    // history). Returns false when the command is unknown.
    bool execute(const std::string& line);

    // --- window ---------------------------------------------------------
    bool visible() const;
    void setVisible(bool v);
    void toggle();
    // Draws the window when visible. Call inside the imgui frame. `title` is
    // also the imgui window id, so two consoles need two titles.
    void draw(const char* title = "Console");

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// Splits an engine log line into (subsystem, message).
//
// Everything in this engine logs as "Subsystem: what happened" -- "Warmup: 96
// materials, 0 unsupported", "RenderCore: walking '...'", "Ogre: Error: ...".
// That convention was invisible to the console, which filed every captured line
// under one category called "engine": the column showed the same word on every
// row, the category filter could not narrow anything, and the eye had no left
// edge to scan. Recovering the prefix is what turns the log back into columns.
//
// Only a plausible subsystem is split off: a short, space-free, non-numeric
// token. Prose that happens to contain a colon ("note: see docs") and paths
// ("C:/x") keep their full text and come back uncategorised.
struct LogSplit {
    std::string category; // empty when the line carries no subsystem prefix
    std::string message;  // the line with that prefix removed
};
LogSplit splitLogCategory(const std::string& line);

} // namespace eng
