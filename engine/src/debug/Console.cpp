#include <eng/debug/Console.h>

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>

namespace eng {
namespace {

// One captured line. Duplicates collapse onto `count` instead of appending,
// which is what keeps a per-frame warning from burying the log.
struct Entry {
    log::Level level = log::Level::Info;
    std::string category;
    std::string text;
    double time = 0.0; // seconds since the console was created
    int count = 1;
    bool command = false; // echoed input, coloured as the user's own line
};

struct Command {
    std::string name;
    std::string help;
    DebugConsole::Handler fn;
    DebugConsole::Completer complete;
};

// Severity colours are the active theme's own accents (Ash Reliquary: brass
// warn, blood error, ritual violet fatal -- see eng::imguitheme), not an
// independent palette: a console painted in its own reds and yellows is what
// makes a debug window look bolted on. They were One Dark values when the tool
// UI was One Dark; keep them in step when the theme changes.
constexpr ImVec4 rgb(unsigned hex, float a = 1.0f)
{
    return ImVec4(float((hex >> 16) & 0xFF) / 255.0f,
                  float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f,
                  a);
}

ImVec4 levelColor(log::Level level, bool command)
{
    if (command)
        return rgb(0xF0B95C); // brass focus: the user's own line
    switch (level) {
    case log::Level::Warn: return rgb(0xD9A441);
    case log::Level::Error: return rgb(0xC74A46);
    case log::Level::Fatal: return rgb(0x8F83C9);
    case log::Level::Info: break;
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

// The tag printed in the level column. Fixed width, because the imgui font is
// monospace and a ragged column is exactly what makes a log unscannable.
//
// A tag rather than colour alone: severity was carried only by a 2px bar and
// the text tint, so every info line -- which is most of them -- looked
// identical to every other, and telling a warning from a note meant comparing
// two shades of brass. Colour is the fast path; the word is the certain one.
const char* levelTag(log::Level level, bool command)
{
    if (command)
        return " > ";
    switch (level) {
    case log::Level::Warn: return "WARN";
    case log::Level::Error: return "ERR ";
    case log::Level::Fatal: return "DEAD";
    case log::Level::Info: break;
    }
    return "info";
}

// Background wash for the rows that matter. An error scrolling past in a wall
// of info is findable by band before it is findable by reading.
bool levelWantsRowTint(log::Level level, bool command)
{
    return !command && level != log::Level::Info;
}

// A filter chip: severity dot + label + count, drawn as a themed toggle rather
// than as coloured text on a button. Returns true when clicked.
bool levelChip(const char* name, int count, bool on, ImVec4 accent)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    char label[64];
    std::snprintf(label, sizeof(label), "%s %d", name, count);
    const float dot = ImGui::GetFontSize() * 0.42f;
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(text.x + dot * 2.0f + style.FramePadding.x * 3.0f,
                      ImGui::GetFrameHeight());

    ImGui::PushStyleColor(ImGuiCol_Button,
                          on ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive)
                             : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          on ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                             : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushID(name);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button("##chip", size);
    ImGui::PopStyleColor(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!on)
        accent.w = 0.35f;
    const ImVec2 centre(origin.x + style.FramePadding.x + dot * 0.5f,
                        origin.y + size.y * 0.5f);
    dl->AddCircleFilled(centre, dot * 0.5f, ImGui::GetColorU32(accent));
    dl->AddText(ImVec2(centre.x + dot, origin.y + (size.y - text.y) * 0.5f),
                ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                label);
    ImGui::PopID();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s: click to %s", name, on ? "hide" : "show");
    return clicked;
}

// Longest prefix accepted as a subsystem name. "ParticleMaterials" is 17; the
// cap keeps a sentence that happens to contain a colon from becoming a category
// wide enough to push every message off the row.
constexpr std::size_t kMaxCategory = 18;

bool containsInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(),
                          needle.end(), [](char a, char b) {
                              return std::tolower((unsigned char)a) ==
                                     std::tolower((unsigned char)b);
                          });
    return it != haystack.end();
}

// Splits on whitespace, honouring double quotes so a path with spaces stays
// one argument.
std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false, has = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            has = true;
            continue;
        }
        if (!inQuotes && std::isspace((unsigned char)c)) {
            if (has)
                out.push_back(cur);
            cur.clear();
            has = false;
            continue;
        }
        cur.push_back(c);
        has = true;
    }
    if (has)
        out.push_back(cur);
    return out;
}

std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

LogSplit splitLogCategory(const std::string& line)
{
    const std::size_t colon = line.find(':');
    if (colon == 0 || colon == std::string::npos || colon > kMaxCategory)
        return {{}, line};
    // The prefix has to be followed by a separator. Without this, a Windows
    // path ("C:/assets") and a time ("12:04 done") both read as categories.
    if (colon + 1 < line.size() && line[colon + 1] != ' ')
        return {{}, line};

    const std::string head = line.substr(0, colon);
    bool hasLetter = false;
    for (const char c : head) {
        // A subsystem is one identifier-ish token: letters, digits, and the
        // separators the engine actually uses in its own names.
        const unsigned char u = (unsigned char)c;
        if (!std::isalnum(u) && c != '_' && c != '.' && c != '-')
            return {{}, line};
        hasLetter = hasLetter || std::isalpha(u) != 0;
    }
    // Digits alone are a measurement, not a subsystem ("42: ..." stays prose).
    if (!hasLetter)
        return {{}, line};

    const std::size_t rest = line.find_first_not_of(' ', colon + 1);
    return {head, rest == std::string::npos ? std::string{}
                                            : line.substr(rest)};
}

struct DebugConsole::Impl {
    // --- log state ---
    std::vector<Entry> entries;
    std::size_t capacity = 4096;
    std::mutex pending_mutex;
    std::vector<Entry> pending; // filled from any thread by the log sink
    int logSink = -1;
    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    // --- command state ---
    std::vector<Command> commands;
    std::vector<std::string> history;
    int historyPos = -1; // -1 = editing a fresh line
    char input[512] = {};

    // --- ui state ---
    bool visible = false;
    bool focusInput = false;
    bool wasVisible = false;
    bool autoScroll = true;
    bool scrollToBottom = false;
    bool showTimestamps = true;
    bool showCategories = true;
    bool wrap = true;
    bool collapse = true;
    bool levelOn[4] = {true, true, true, true};
    int levelCount[4] = {0, 0, 0, 0};
    char filter[128] = {};
    std::string categoryFilter;
    std::vector<int> filtered; // indices into `entries`
    bool filterDirty = true;

    double now() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             start)
            .count();
    }

    void push(Entry e)
    {
        if (collapse && !entries.empty()) {
            Entry& last = entries.back();
            if (last.level == e.level && last.text == e.text &&
                last.category == e.category && last.command == e.command) {
                ++last.count;
                last.time = e.time;
                filterDirty = true;
                scrollToBottom = autoScroll;
                return;
            }
        }
        ++levelCount[int(e.level)];
        entries.push_back(std::move(e));
        if (entries.size() > capacity) {
            const std::size_t drop = entries.size() - capacity;
            for (std::size_t i = 0; i < drop; ++i)
                --levelCount[int(entries[i].level)];
            entries.erase(entries.begin(), entries.begin() + long(drop));
        }
        filterDirty = true;
        scrollToBottom = autoScroll;
    }

    void drainPending()
    {
        std::vector<Entry> taken;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            taken.swap(pending);
        }
        for (Entry& e : taken)
            push(std::move(e));
    }

    bool passes(const Entry& e) const
    {
        if (!levelOn[int(e.level)])
            return false;
        if (!categoryFilter.empty() && e.category != categoryFilter)
            return false;
        if (filter[0] == '\0')
            return true;
        const std::string needle(filter);
        return containsInsensitive(e.text, needle) ||
               containsInsensitive(e.category, needle);
    }

    void rebuildFilter()
    {
        filtered.clear();
        filtered.reserve(entries.size());
        for (int i = 0; i < int(entries.size()); ++i)
            if (passes(entries[i]))
                filtered.push_back(i);
        filterDirty = false;
    }

    Command* find(const std::string& name)
    {
        for (Command& c : commands)
            if (c.name == name)
                return &c;
        return nullptr;
    }

    std::vector<std::string> candidates(const std::string& prefix) const
    {
        std::vector<std::string> out;
        for (const Command& c : commands)
            if (c.name.compare(0, prefix.size(), prefix) == 0)
                out.push_back(c.name);
        return out;
    }

    int textCallback(ImGuiInputTextCallbackData* data);
    // imgui wants a plain function pointer; Impl is private, so the thunk has
    // to live in here rather than in the anonymous namespace above.
    static int thunk(ImGuiInputTextCallbackData* data)
    {
        return static_cast<Impl*>(data->UserData)->textCallback(data);
    }
};

// Tab completes; Up/Down walk the history. Both edit the buffer in place,
// which is why this has to be a callback and not post-Enter handling.
int DebugConsole::Impl::textCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Only the command name completes generically; an argument completes
        // through the command's own Completer.
        std::string line(data->Buf, data->Buf + data->BufTextLen);
        const bool trailingSpace =
            !line.empty() && std::isspace((unsigned char)line.back());
        std::vector<std::string> tokens = tokenize(line);
        std::vector<std::string> cands;
        std::size_t replaceFrom = 0;
        if (tokens.empty() || (tokens.size() == 1 && !trailingSpace)) {
            const std::string prefix = tokens.empty() ? std::string() : tokens[0];
            cands = candidates(prefix);
            replaceFrom = line.size() - prefix.size();
        } else if (Command* c = find(tokens[0]); c && c->complete) {
            const std::string prefix = trailingSpace ? std::string() : tokens.back();
            for (std::string& s : c->complete(tokens))
                if (s.compare(0, prefix.size(), prefix) == 0)
                    cands.push_back(std::move(s));
            replaceFrom = line.size() - prefix.size();
        }
        if (cands.empty())
            return 0;
        // Complete to the longest common prefix; only when that adds nothing
        // is the ambiguity worth printing.
        std::string common = cands.front();
        for (const std::string& s : cands)
            while (s.compare(0, common.size(), common) != 0)
                common.pop_back();
        const std::string current = line.substr(replaceFrom);
        if (common.size() > current.size()) {
            data->DeleteChars(int(replaceFrom), data->BufTextLen - int(replaceFrom));
            data->InsertChars(data->CursorPos, common.c_str());
            if (cands.size() == 1)
                data->InsertChars(data->CursorPos, " ");
        } else if (cands.size() > 1) {
            std::string list;
            for (const std::string& s : cands)
                list += s + "  ";
            Entry e;
            e.time = now();
            e.category = "console";
            e.text = "candidates: " + list;
            push(std::move(e));
        }
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const int prev = historyPos;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (historyPos == -1)
                historyPos = int(history.size()) - 1;
            else if (historyPos > 0)
                --historyPos;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (historyPos != -1 && ++historyPos >= int(history.size()))
                historyPos = -1;
        }
        if (prev != historyPos) {
            const std::string line =
                historyPos >= 0 ? history[std::size_t(historyPos)] : std::string();
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, line.c_str());
        }
    }
    return 0;
}

DebugConsole::DebugConsole() : mImpl(std::make_unique<Impl>())
{
    // PSX_CONSOLE=1 opens it on the first frame: a headless capture has no key
    // to press, and a crash during scene build has no later frame to press it
    // in either.
    mImpl->visible = std::getenv("PSX_CONSOLE") != nullptr;

    registerCommand("help", "list commands, or explain one",
                    [this](const Args& a) {
                        if (a.size() > 1) {
                            if (Command* c = mImpl->find(a[1]))
                                print(log::Level::Info, "console",
                                      c->name + " -- " + c->help);
                            else
                                print(log::Level::Warn, "console",
                                      "no such command: " + a[1]);
                            return;
                        }
                        print(log::Level::Info, "console",
                              "commands (Tab completes, Up/Down for history):");
                        for (const Command& c : mImpl->commands)
                            print(log::Level::Info, "console",
                                  "  " + c.name + "  -- " + c.help);
                    },
                    [this](const Args&) {
                        std::vector<std::string> out;
                        for (const Command& c : mImpl->commands)
                            out.push_back(c.name);
                        return out;
                    });
    registerCommand("clear", "empty the log", [this](const Args&) { clear(); });
    registerCommand("echo", "print the rest of the line",
                    [this](const Args& a) {
                        std::string s;
                        for (std::size_t i = 1; i < a.size(); ++i)
                            s += (i > 1 ? " " : "") + a[i];
                        print(log::Level::Info, "console", s);
                    });
    registerCommand("history", "list previously run commands",
                    [this](const Args&) {
                        for (const std::string& h : mImpl->history)
                            print(log::Level::Info, "console", "  " + h);
                    });
}

DebugConsole::~DebugConsole()
{
    if (mImpl && mImpl->logSink >= 0)
        log::removeSink(mImpl->logSink);
}

DebugConsole::DebugConsole(DebugConsole&&) noexcept = default;
DebugConsole& DebugConsole::operator=(DebugConsole&&) noexcept = default;

void DebugConsole::captureEngineLog()
{
    if (mImpl->logSink >= 0)
        return;
    // The sink can fire from any thread and long after this frame, so it only
    // queues; `this` stays valid because the sink is removed in the dtor.
    Impl* impl = mImpl.get();
    mImpl->logSink = log::addSink([impl](log::Level level, const char* text) {
        Entry e;
        e.level = level;
        // "Warmup: 96 materials" becomes category "Warmup" + the message, so
        // the column carries the subsystem instead of the word "engine" on
        // every row. Uncategorised lines keep their full text.
        LogSplit split = splitLogCategory(text);
        e.category = std::move(split.category);
        e.text = std::move(split.message);
        e.time = impl->now();
        std::lock_guard<std::mutex> lock(impl->pending_mutex);
        impl->pending.push_back(std::move(e));
    });
}

void DebugConsole::print(log::Level level, std::string category,
                         std::string text)
{
    Entry e;
    e.level = level;
    e.category = std::move(category);
    e.text = std::move(text);
    e.time = mImpl->now();
    mImpl->push(std::move(e));
}

void DebugConsole::printf(log::Level level, const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(level, {}, buf);
}

void DebugConsole::clear()
{
    mImpl->entries.clear();
    for (int& c : mImpl->levelCount)
        c = 0;
    mImpl->filterDirty = true;
}

void DebugConsole::setCapacity(std::size_t lines)
{
    mImpl->capacity = std::max<std::size_t>(lines, 16);
}

void DebugConsole::registerCommand(std::string name, std::string help,
                                   Handler fn, Completer complete)
{
    if (Command* existing = mImpl->find(name)) {
        existing->help = std::move(help);
        existing->fn = std::move(fn);
        existing->complete = std::move(complete);
        return;
    }
    mImpl->commands.push_back(
        {std::move(name), std::move(help), std::move(fn), std::move(complete)});
    std::sort(mImpl->commands.begin(), mImpl->commands.end(),
              [](const Command& a, const Command& b) { return a.name < b.name; });
}

void DebugConsole::bindBool(std::string name, bool* value, std::string help)
{
    const std::string n = name;
    registerCommand(
        std::move(name), help.empty() ? "bool (0/1)" : std::move(help),
        [this, value, n](const Args& a) {
            if (a.size() > 1)
                *value = !(a[1] == "0" || a[1] == "false" || a[1] == "off");
            print(log::Level::Info, "cvar",
                  n + " = " + (*value ? "1" : "0"));
        },
        [](const Args&) {
            return std::vector<std::string>{"0", "1"};
        });
}

void DebugConsole::bindInt(std::string name, int* value, int lo, int hi,
                           std::string help)
{
    const std::string n = name;
    registerCommand(std::move(name),
                    help.empty() ? "int" : std::move(help),
                    [this, value, lo, hi, n](const Args& a) {
                        if (a.size() > 1)
                            *value = std::clamp(std::atoi(a[1].c_str()), lo, hi);
                        print(log::Level::Info, "cvar",
                              n + " = " + std::to_string(*value) + "  [" +
                                  std::to_string(lo) + ".." +
                                  std::to_string(hi) + "]");
                    });
}

void DebugConsole::bindFloat(std::string name, float* value, float lo, float hi,
                             std::string help)
{
    const std::string n = name;
    registerCommand(std::move(name),
                    help.empty() ? "float" : std::move(help),
                    [this, value, lo, hi, n](const Args& a) {
                        if (a.size() > 1)
                            *value = std::clamp(float(std::atof(a[1].c_str())),
                                                lo, hi);
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "%s = %.4g  [%.4g..%.4g]",
                                      n.c_str(), double(*value), double(lo),
                                      double(hi));
                        print(log::Level::Info, "cvar", buf);
                    });
}

bool DebugConsole::execute(const std::string& line)
{
    const std::string cmd = trim(line);
    if (cmd.empty())
        return true;

    Entry echo;
    echo.time = mImpl->now();
    echo.category = "console";
    echo.text = "> " + cmd;
    echo.command = true;
    mImpl->push(std::move(echo));

    // Most recent last, no adjacent duplicates: Up then Enter should repeat,
    // not fill history with the same line.
    if (mImpl->history.empty() || mImpl->history.back() != cmd)
        mImpl->history.push_back(cmd);
    mImpl->historyPos = -1;

    const Args args = tokenize(cmd);
    Command* c = args.empty() ? nullptr : mImpl->find(args[0]);
    if (!c) {
        print(log::Level::Error, "console",
              "unknown command: " + (args.empty() ? cmd : args[0]) +
                  " (try 'help')");
        return false;
    }
    c->fn(args);
    return true;
}

bool DebugConsole::visible() const { return mImpl->visible; }
void DebugConsole::setVisible(bool v) { mImpl->visible = v; }
void DebugConsole::toggle() { mImpl->visible = !mImpl->visible; }

void DebugConsole::draw(const char* title)
{
    Impl& s = *mImpl;
    s.drainPending(); // even while hidden: the log must not lose lines

    if (!s.visible) {
        s.wasVisible = false;
        return;
    }
    if (!s.wasVisible) {
        s.focusInput = true; // opening the console means typing in it
        s.wasVisible = true;
        ImGui::SetNextWindowFocus(); // docked: also selects its tab
    }

    // Placed as well as sized. With only a size, imgui puts a new window at a
    // fixed offset that the dockspace then shifts, so the console opened half
    // off the left edge and its timestamp/level columns were the part that fell
    // outside -- the log looked like it had no metadata at all.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size(std::min(760.0f, vp->WorkSize.x - 32.0f),
                      std::min(420.0f, vp->WorkSize.y - 32.0f));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + 16.0f,
               vp->WorkPos.y + vp->WorkSize.y - size.y - 16.0f),
        ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &s.visible)) {
        ImGui::End();
        return;
    }

    // --- toolbar --------------------------------------------------------
    // The filter takes whatever the chips do not: measured, not guessed, so a
    // different font or theme padding cannot push "options" off the edge.
    const char* names[4] = {"info", "warn", "error", "fatal"};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float dot = ImGui::GetFontSize() * 0.42f;
    float chipsWidth = ImGui::CalcTextSize("options").x +
                       style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    for (int i = 0; i < 4; ++i) {
        char label[64];
        std::snprintf(label, sizeof(label), "%s %d", names[i], s.levelCount[i]);
        chipsWidth += ImGui::CalcTextSize(label).x + dot * 2.0f +
                      style.FramePadding.x * 3.0f + style.ItemSpacing.x;
    }
    ImGui::SetNextItemWidth(std::max(
        140.0f, ImGui::GetContentRegionAvail().x - chipsWidth - style.ItemSpacing.x));
    if (ImGui::InputTextWithHint("##filter", "filter (substring, any field)",
                                 s.filter, sizeof(s.filter)))
        s.filterDirty = true;
    ImGui::SameLine();

    // Level chips double as counters: the number is why you look at them.
    for (int i = 0; i < 4; ++i) {
        if (levelChip(names[i], s.levelCount[i], s.levelOn[i],
                      levelColor(log::Level(i), false))) {
            s.levelOn[i] = !s.levelOn[i];
            s.filterDirty = true;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("options"))
        ImGui::OpenPopup("console_options");
    if (ImGui::BeginPopup("console_options")) {
        ImGui::Checkbox("auto-scroll", &s.autoScroll);
        ImGui::Checkbox("timestamps", &s.showTimestamps);
        ImGui::Checkbox("categories", &s.showCategories);
        ImGui::Checkbox("word wrap", &s.wrap);
        if (ImGui::Checkbox("collapse repeats", &s.collapse))
            s.filterDirty = true;
        int cap = int(s.capacity);
        if (ImGui::DragInt("scrollback", &cap, 64.0f, 64, 65536))
            setCapacity(std::size_t(cap));
        ImGui::Separator();
        if (ImGui::MenuItem("copy visible to clipboard")) {
            std::string all;
            for (int idx : s.filtered)
                all += s.entries[std::size_t(idx)].text + "\n";
            ImGui::SetClipboardText(all.c_str());
        }
        if (ImGui::MenuItem("clear"))
            clear();
        ImGui::EndPopup();
    }
    if (!s.categoryFilter.empty()) {
        ImGui::SameLine();
        char label[96];
        std::snprintf(label, sizeof(label), "[%s]  x##catfilter",
                      s.categoryFilter.c_str());
        if (ImGui::SmallButton(label)) {
            s.categoryFilter.clear();
            s.filterDirty = true;
        }
    }

    // --- log ------------------------------------------------------------
    if (s.filterDirty)
        s.rebuildFilter();

    const float footer = ImGui::GetFrameHeightWithSpacing() +
                         ImGui::GetStyle().ItemSpacing.y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginChild("scroll", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders,
                      s.wrap ? 0 : ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    // Clipper, not a plain loop: a full scrollback is tens of thousands of
    // lines and only ~40 of them are on screen.
    ImGuiListClipper clipper;
    clipper.Begin(int(s.filtered.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Entry& e = s.entries[std::size_t(s.filtered[std::size_t(row)])];
            ImGui::PushID(row);
            // The default imgui font is monospace, so fixed-width formats align
            // the metadata columns without any manual x placement -- which is
            // what the earlier hand-computed offsets kept getting wrong.
            const ImVec4 col = levelColor(e.level, e.command);
            const float lh = ImGui::GetTextLineHeight();
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            // Warnings and above get a wash across the whole row. Scrolling a
            // thousand info lines, a band is findable before any text is.
            if (levelWantsRowTint(e.level, e.command))
                dl->AddRectFilled(
                    ImVec2(rowStart.x - 4.0f, rowStart.y),
                    ImVec2(rowStart.x + rowWidth, rowStart.y + lh + 2.0f),
                    ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.10f)));
            // Severity bar: full strength for everything that is not routine,
            // a hint for info. It is the colour half of the level read; the
            // tag below is the half that survives a colourblind viewer and a
            // washed-out capture.
            dl->AddRectFilled(
                ImVec2(rowStart.x, rowStart.y + 1.0f),
                ImVec2(rowStart.x + 2.0f, rowStart.y + lh - 1.0f),
                ImGui::GetColorU32(e.level == log::Level::Info && !e.command
                                       ? ImVec4(col.x, col.y, col.z, 0.20f)
                                       : col),
                1.0f);
            ImGui::Dummy(ImVec2(6.0f, lh));
            ImGui::SameLine(0.0f, 0.0f);
            if (s.showTimestamps) {
                ImGui::TextDisabled("%7.2f", e.time);
                ImGui::SameLine();
            }
            // Level tag, in the severity colour. Info is dimmed on purpose:
            // the routine case should recede so the exceptions stand out.
            if (e.level == log::Level::Info && !e.command)
                ImGui::TextDisabled("%-4s", levelTag(e.level, e.command));
            else
                ImGui::TextColored(col, "%-4s", levelTag(e.level, e.command));
            ImGui::SameLine();
            if (s.showCategories) {
                // Padded even when empty, so an uncategorised line does not
                // shift the message column left and break the block of text.
                // Wide enough for the longest real subsystem name here
                // ("ParticleMaterials"): truncating to 12 turned that into
                // "ParticleMate", which reads as a different subsystem from
                // "ParticleLibr" only if you already know both.
                ImGui::TextDisabled("%-17.17s", e.category.c_str());
                if (!e.category.empty() && ImGui::IsItemClicked()) {
                    // Click the subsystem to isolate it. The one gesture that
                    // turns "everything scrolled past" into "just the renderer".
                    s.categoryFilter =
                        s.categoryFilter == e.category ? std::string{}
                                                       : e.category;
                    s.filterDirty = true;
                }
                if (!e.category.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s: click to %s", e.category.c_str(),
                                      s.categoryFilter == e.category
                                          ? "clear the filter"
                                          : "show only this subsystem");
                }
                ImGui::SameLine();
            }
            // The message keeps the default text colour at info: a whole log
            // tinted by severity is a log with no hierarchy, because the eye
            // has nothing to compare the tint against.
            if (e.level != log::Level::Info || e.command)
                ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (s.wrap)
                ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(e.text.c_str());
            if (s.wrap)
                ImGui::PopTextWrapPos();
            if (e.level != log::Level::Info || e.command)
                ImGui::PopStyleColor();
            if (e.count > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("x%d", e.count);
            }
            if (ImGui::BeginPopupContextItem("row")) {
                if (ImGui::MenuItem("copy line"))
                    ImGui::SetClipboardText(e.text.c_str());
                if (!e.category.empty() &&
                    ImGui::MenuItem(("filter to [" + e.category + "]").c_str())) {
                    s.categoryFilter = e.category;
                    s.filterDirty = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::PopStyleVar();

    // Stick to the bottom only when already there, so scrolling back to read
    // something is not yanked away by the next log line.
    if (s.scrollToBottom ||
        (s.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        ImGui::SetScrollHereY(1.0f);
    s.scrollToBottom = false;
    ImGui::EndChild();

    // --- input line -----------------------------------------------------
    const ImGuiInputTextFlags flags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_EscapeClearsAll |
        ImGuiInputTextFlags_CallbackCompletion |
        ImGuiInputTextFlags_CallbackHistory;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    // Extra horizontal frame padding: the caret sits on top of the first glyph
    // at the default padding, which reads as a clipped letter.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, ImGui::GetStyle().FramePadding.y));
    if (ImGui::InputTextWithHint("##input", "command  (Tab completes, Up/Down history)",
                                 s.input, sizeof(s.input), flags, Impl::thunk,
                                 &s)) {
        execute(s.input);
        s.input[0] = '\0';
        s.focusInput = true;
    }
    ImGui::PopStyleVar();
    ImGui::SetItemDefaultFocus();
    if (s.focusInput) {
        ImGui::SetKeyboardFocusHere(-1);
        s.focusInput = false;
    }

    ImGui::End();
}

} // namespace eng
