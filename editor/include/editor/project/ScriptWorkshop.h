#pragma once

#include <string>
#include <vector>

namespace ed {

// Authoring Lua from the editor: making a script, opening it, and seeing what
// it said when it broke.
//
// The editor deliberately does NOT edit script text. A code editor is a large
// thing to build badly, and everyone already has one they prefer -- so the
// editor's job is the parts a text editor cannot do: put the file in the right
// place with the right shape in it, attach it to an entity, and show the errors
// the running game produced against the scene those errors happened in.

// The template a new script starts from.
//
// Not empty, and not a bare `return {}`. A script's shape -- a class table with
// callbacks, returned at the end -- is the one thing about this system somebody
// has to know before they can write anything, and a file that demonstrates it
// costs nothing to generate and saves reading the docs first.
std::string scriptTemplate(const std::string& className);

// Writes `scriptTemplate(className)` to `absolutePath`.
//
// Refuses to overwrite: a "new script" button that silently replaces somebody's
// work is unforgivable, and the failure is easy to hit by picking a name twice.
// Creates parent directories, because a project's scripts/ may not exist yet.
bool createScript(const std::string& absolutePath, const std::string& className,
                  std::string& error);

// "scripts/my_door.lua" from an entity called "My Door". Lowercase, spaces and
// punctuation to underscores, so the file name matches what the outliner shows
// without being something a shell has to be told about.
std::string suggestedScriptPath(const std::string& entityName);

// The class name for that file: "MyDoor" from "my_door.lua".
std::string classNameFromPath(const std::string& path);

// One script error, parsed out of a playtest log.
//
// The runtime writes these through eng::script::reportScriptError, which is
// deliberately one log call per error including the traceback -- so a parser
// can find the whole of one rather than guessing where the next begins.
struct ScriptIssue {
    std::string script;   // "scripts/door.lua", as the log names it
    std::string subject;  // "entity 'Door' #12", empty for a non-entity error
    std::string callback; // "update", "start", "callback"
    std::string message;  // the Lua error, first line
    std::string detail;   // the full traceback, if the log carried one
};

// Every script error in `logText`, oldest first.
//
// Takes the text rather than a path so it is testable without a file and so the
// caller decides how much of a long log to keep. Lines that are not script
// errors are ignored, which is most of a playtest log.
std::vector<ScriptIssue> parseScriptIssues(const std::string& logText);

// The last `maxBytes` of a file, as text. Empty when it cannot be read.
//
// The tail, not the whole thing: a playtest that ran for an hour has a log
// nobody wants loaded into the editor, and the errors that matter are the
// recent ones.
std::string tailFile(const std::string& path, std::size_t maxBytes = 64 * 1024);

// Opens `path` in whatever the author edits Lua with: $VISUAL, then $EDITOR,
// then the desktop's default handler.
//
// Detached and never waited on -- an editor that blocks until somebody closes
// vim is an editor that looks hung. Returns false only when nothing could be
// launched at all.
bool openInExternalEditor(const std::string& path, std::string& error);

} // namespace ed
