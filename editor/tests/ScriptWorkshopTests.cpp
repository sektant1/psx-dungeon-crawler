// Making a script, naming one, and reading errors back out of a playtest log.
//
// The log parsing is the part worth testing hard: its input is produced by
// eng::script::reportScriptError in another process, so the two agree by
// convention rather than by a shared type, and nothing but a test keeps them
// agreeing.

#include <editor/project/ScriptWorkshop.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;
namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ScriptWorkshopTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path scratch()
{
    const fs::path dir = fs::temp_directory_path() / "raven_script_workshop";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

static void testNaming()
{
    require(suggestedScriptPath("Door") == "scripts/door.lua", "simple name");
    require(suggestedScriptPath("My Door") == "scripts/my_door.lua",
            "spaces become underscores");
    // Runs of punctuation collapse, and a trailing one is dropped: the result
    // has to be something a shell never needs to be told about.
    require(suggestedScriptPath("My  Door!") == "scripts/my_door.lua",
            "runs of punctuation collapse");
    require(suggestedScriptPath("") == "scripts/behaviour.lua",
            "an unnamed entity still gets a path");
    require(suggestedScriptPath("!!!") == "scripts/behaviour.lua",
            "and so does one with no usable characters");

    require(classNameFromPath("scripts/my_door.lua") == "MyDoor",
            "the class name is the file name in camel case");
    require(classNameFromPath("scripts/spin.lua") == "Spin", "one word");
    require(classNameFromPath("") == "Behaviour", "always something");
}

// The generated file has to be a working script, not a stub: the shape is the
// one thing somebody has to know before they can write anything.
static void testTemplate()
{
    const std::string text = scriptTemplate("MyDoor");
    require(text.find("local MyDoor = {}") != std::string::npos,
            "the class table is declared");
    require(text.find("function MyDoor:start()") != std::string::npos,
            "start is written out, not commented");
    require(text.find("function MyDoor:update(dt)") != std::string::npos,
            "and update");
    require(text.find("return MyDoor") != std::string::npos,
            "and the chunk returns the class, which is what makes it load");
    require(text.find("self.props") != std::string::npos,
            "authored props are shown, since that is what the inspector edits");
}

static void testCreate()
{
    const fs::path dir = scratch();
    // Into a subdirectory that does not exist: a new project has no scripts/.
    const fs::path file = dir / "scripts" / "door.lua";

    std::string error;
    require(createScript(file.string(), "Door", error), error.c_str());
    require(fs::is_regular_file(file), "the file is written");

    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    require(text.find("local Door = {}") != std::string::npos,
            "with the requested class name");

    // Never overwrite: picking a name twice must not destroy the first script.
    require(!createScript(file.string(), "Door", error),
            "creating over an existing script is refused");
    require(!error.empty(), "and says why");
}

// One error, with an entity subject, as the runtime writes it.
static void testParseOneIssue()
{
    const std::string log =
        "[info] Engine: render profile 'psx'\n"
        "[error] Script: scripts/door.lua on entity 'Door' #12 in update():\n"
        "  scripts/door.lua:14: attempt to index a nil value (field 'target')\n"
        "stack traceback:\n"
        "\t[C]: in metamethod 'index'\n"
        "\tscripts/door.lua:14: in function <scripts/door.lua:12>\n"
        "[info] Load: 4 steps in 75 ms\n";

    const std::vector<ScriptIssue> issues = parseScriptIssues(log);
    require(issues.size() == 1, "exactly one error is found");
    const ScriptIssue& issue = issues[0];
    require(issue.script == "scripts/door.lua", "the script is named");
    require(issue.subject == "entity 'Door' #12", "and the entity");
    require(issue.callback == "update", "and the callback, without the ()");
    require(issue.message.find("attempt to index a nil value") !=
                std::string::npos,
            "the message is the first indented line");
    require(issue.detail.find("stack traceback") != std::string::npos,
            "the traceback is kept, since that is what says where");
}

// An error with no entity behind it -- a timer callback, or a load failure.
static void testParseWithoutSubject()
{
    const std::string log =
        "[error] Script: timer in callback():\n"
        "  scripts/boom.lua:5: boom\n";

    const std::vector<ScriptIssue> issues = parseScriptIssues(log);
    require(issues.size() == 1, "found");
    require(issues[0].script == "timer", "the source is named");
    require(issues[0].subject.empty(), "with no subject");
    require(issues[0].callback == "callback", "and the callback");
}

static void testParseMany()
{
    const std::string log =
        "[error] Script: scripts/a.lua in start():\n  a.lua:1: first\n"
        "[info] something else entirely\n"
        "[error] Script: scripts/b.lua in update():\n  b.lua:2: second\n";

    const std::vector<ScriptIssue> issues = parseScriptIssues(log);
    require(issues.size() == 2, "both are found");
    // Oldest first: the first error is usually the cause and the rest the
    // consequences, so reordering them would bury the useful one.
    require(issues[0].script == "scripts/a.lua", "in log order");
    require(issues[1].script == "scripts/b.lua", "oldest first");
}

static void testParseIgnoresEverythingElse()
{
    const std::string log =
        "[info] assets: root /x (dev), 1 packs\n"
        "[warn] Script: hot reload is on but 'scripts' does not resolve\n"
        "[error] Renderer: material 'X' is missing; using 'Y'\n";
    require(parseScriptIssues(log).empty(),
            "warnings and non-script errors are not script errors");
}

// The tail must not hand the parser half a record.
static void testTail()
{
    const fs::path dir = scratch();
    const fs::path file = dir / "playtest.log";
    {
        std::ofstream out(file);
        for (int i = 0; i < 500; ++i)
            out << "[info] line " << i << " padding padding padding\n";
        out << "[error] Script: scripts/last.lua in update():\n"
            << "  last.lua:3: boom\n";
    }

    const std::string tail = tailFile(file.string(), 512);
    require(!tail.empty(), "something came back");
    require(tail.size() <= 512, "and no more than asked for");
    require(tail.find("scripts/last.lua") != std::string::npos,
            "the tail keeps the newest lines, which is where errors are");
    require(tail[0] == '[',
            "and begins at a line boundary, so no half record is parsed");

    require(parseScriptIssues(tail).size() == 1, "the tail parses");
    require(tailFile((dir / "missing.log").string()).empty(),
            "a missing log is empty, not an error");
}

int main()
{
    testNaming();
    testTemplate();
    testCreate();
    testParseOneIssue();
    testParseWithoutSubject();
    testParseMany();
    testParseIgnoresEverythingElse();
    testTail();
    std::puts("ScriptWorkshopTests: ok");
    return 0;
}
