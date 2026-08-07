#include <editor/project/ScriptWorkshop.h>

#include <eng/Log.h>

#include <spawn.h>
#include <sys/wait.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

extern char** environ;

namespace fs = std::filesystem;

namespace ed {
namespace {

// The prefix eng::script::reportScriptError writes. Matching the log's own
// wording rather than a code, because that function's format IS the contract
// between the runtime and this parser -- there is no structured channel between
// a child process and the editor, and inventing one for this would be a lot of
// machinery for a feature whose input is already a file on disk.
constexpr const char* kPrefix = "[error] Script: ";

std::string trimmed(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

} // namespace

std::string scriptTemplate(const std::string& className)
{
    const std::string name = className.empty() ? "Behaviour" : className;
    std::string out;
    out += "-- " + name + "\n";
    out += R"(--
-- A script is a class table: methods are shared by every entity that carries
-- this file, and state lives on `self`, which is one table per entity. Never
-- put per-entity state at file scope -- see docs/scripting.md.
--
-- Every callback is optional. Delete what this does not need.

local )";
    out += name;
    out += " = {}\n\n";
    out += "function " + name + R"(:start()
  -- Once, on the first tick after this script is attached. The scene is fully
  -- built by now, so it is safe to look things up.
  --
  -- `self.props` holds the values authored on this instance in the inspector.
  self.speed = self.props.speed or 1.0
end

)";
    out += "function " + name + R"(:update(dt)
  -- Every frame, in game time: dt is already scaled and is zero while paused.
end

-- function )";
    out += name + R"(:fixed_update(dt) end   -- before each physics step
-- function )";
    out += name + R"(:on_collision(other, hit) end
-- function )";
    out += name + R"(:on_trigger(other) end  -- a sensor collider was entered
-- function )";
    out += name + R"(:on_event(name, data) end
-- function )";
    out += name + R"(:on_destroy() end

return )";
    out += name + "\n";
    return out;
}

bool createScript(const std::string& absolutePath, const std::string& className,
                  std::string& error)
{
    error.clear();
    const fs::path path(absolutePath);
    std::error_code ec;

    if (fs::exists(path, ec)) {
        // Never overwrite. Picking a name twice is easy, and the second time
        // would silently destroy the first script.
        error = path.filename().string() + " already exists";
        return false;
    }
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "could not create " + path.parent_path().string() + ": " +
                    ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "could not write " + absolutePath;
        return false;
    }
    out << scriptTemplate(className);
    if (!out) {
        error = "failed while writing " + absolutePath;
        return false;
    }
    return true;
}

std::string suggestedScriptPath(const std::string& entityName)
{
    std::string stem;
    bool lastWasUnderscore = false;
    for (const char c : entityName) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            stem += char(std::tolower(static_cast<unsigned char>(c)));
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore && !stem.empty()) {
            // Runs of punctuation collapse to one separator, and a leading one
            // is dropped: "My  Door!" is my_door, not my__door_.
            stem += '_';
            lastWasUnderscore = true;
        }
    }
    while (!stem.empty() && stem.back() == '_')
        stem.pop_back();
    if (stem.empty())
        stem = "behaviour";
    return "scripts/" + stem + ".lua";
}

std::string classNameFromPath(const std::string& path)
{
    const std::string stem = fs::path(path).stem().string();
    std::string name;
    bool capitalise = true;
    for (const char c : stem) {
        if (c == '_' || c == '-' || c == ' ') {
            capitalise = true;
            continue;
        }
        name += capitalise ? char(std::toupper(static_cast<unsigned char>(c)))
                           : c;
        capitalise = false;
    }
    return name.empty() ? "Behaviour" : name;
}

std::vector<ScriptIssue> parseScriptIssues(const std::string& logText)
{
    std::vector<ScriptIssue> issues;
    std::istringstream in(logText);
    std::string line;

    while (std::getline(in, line)) {
        const std::size_t at = line.find(kPrefix);
        if (at == std::string::npos)
            continue;

        // "<path> on <subject> in <callback>():" or "<path> in <callback>():"
        std::string head = line.substr(at + std::strlen(kPrefix));
        if (!head.empty() && head.back() == ':')
            head.pop_back();

        ScriptIssue issue;
        const std::size_t inAt = head.rfind(" in ");
        if (inAt != std::string::npos) {
            issue.callback = head.substr(inAt + 4);
            if (issue.callback.size() > 2 &&
                issue.callback.substr(issue.callback.size() - 2) == "()")
                issue.callback.resize(issue.callback.size() - 2);
            head = head.substr(0, inAt);
        }
        const std::size_t onAt = head.find(" on ");
        if (onAt != std::string::npos) {
            issue.subject = head.substr(onAt + 4);
            head = head.substr(0, onAt);
        }
        issue.script = trimmed(head);

        // The message and traceback are the indented lines that follow, until
        // the next log line. reportScriptError emits them as one call, so this
        // is reading back exactly what it wrote.
        std::string detail;
        while (in.peek() == ' ' || in.peek() == '\t') {
            if (!std::getline(in, line))
                break;
            if (issue.message.empty())
                issue.message = trimmed(line);
            else
                detail += trimmed(line) + "\n";
        }
        // A traceback continues past the indented block: "stack traceback:" and
        // its frames are flush left in Lua's own output.
        if (in.peek() == 's') {
            const std::streampos mark = in.tellg();
            if (std::getline(in, line) && line.rfind("stack traceback", 0) == 0) {
                detail += line + "\n";
                while (std::getline(in, line)) {
                    if (line.empty() || line[0] == '[')
                        break; // the next log record
                    detail += trimmed(line) + "\n";
                }
            } else if (mark != std::streampos(-1)) {
                in.seekg(mark);
            }
        }
        issue.detail = std::move(detail);
        issues.push_back(std::move(issue));
    }
    return issues;
}

std::string tailFile(const std::string& path, std::size_t maxBytes)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return {};
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return {};
    const std::streamoff want =
        std::min<std::streamoff>(size, std::streamoff(maxBytes));
    in.seekg(size - want);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    // A tail almost always begins mid-line. Dropping the partial one keeps the
    // parser from reading half a record as a whole one.
    if (want < size) {
        const std::size_t nl = text.find('\n');
        if (nl != std::string::npos)
            text.erase(0, nl + 1);
    }
    return text;
}

bool openInExternalEditor(const std::string& path, std::string& error)
{
    error.clear();

    // $VISUAL and $EDITOR first: somebody who has set either has said what they
    // want, and a desktop handler that opens .lua in a web browser is a real
    // configuration people have. xdg-open last, because it is the one that
    // works with no configuration at all.
    std::vector<std::string> candidates;
    if (const char* visual = std::getenv("VISUAL"); visual && *visual)
        candidates.emplace_back(visual);
    if (const char* editor = std::getenv("EDITOR"); editor && *editor)
        candidates.emplace_back(editor);
    candidates.emplace_back("xdg-open");

    for (const std::string& command : candidates) {
        std::string program = command;
        std::string file = path;
        char* argv[] = {program.data(), file.data(), nullptr};
        pid_t pid = 0;
        // Detached, never waited on: an editor that blocks until somebody
        // closes vim is an editor that looks hung. The child is reaped by init.
        const int status =
            posix_spawnp(&pid, program.c_str(), nullptr, nullptr, argv, environ);
        if (status == 0) {
            eng::log::info("Editor: opened %s with %s", path.c_str(),
                           program.c_str());
            return true;
        }
    }

    error = "no editor could be launched; set $EDITOR";
    return false;
}

} // namespace ed
