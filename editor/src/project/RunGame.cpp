#include <editor/project/RunGame.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace ed {

std::string siblingExecutable(const std::string& argv0, const std::string& name)
{
    std::error_code code;
    const std::filesystem::path self =
        std::filesystem::absolute(std::filesystem::path(argv0), code);
    return (self.parent_path() / name).string();
}

std::vector<std::string> playtestEnvironment(const PlaytestEnvironment& options)
{
    std::vector<std::string> env;
    if (!options.playFrom.empty())
        env.push_back("RAVEN_PLAY_FROM=" + options.playFrom);
    if (!options.renderPreset.empty())
        env.push_back("RAVEN_RENDER_PRESET=" + options.renderPreset);
    // Presence, not value: the game tests getenv() != nullptr for these, so
    // exporting them as "0" would turn them ON.
    if (options.console)
        env.push_back("RAVEN_DEBUG_UI=1");
    if (options.colliders)
        env.push_back("RAVEN_SHOW_COLLIDERS=1");
    if (options.fullscreen)
        env.push_back("RAVEN_FULLSCREEN=1");
    return env;
}

RunHandle launchGame(const std::string& gameExe, const std::string& mapPath,
                     const std::string& logPath,
                     const std::vector<std::string>& env)
{
    RunHandle handle;
    std::error_code code;
    if (!std::filesystem::exists(gameExe, code)) {
        handle.error = "game executable not found: " + gameExe;
        return handle;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Both streams to one log: a launch that dies on a missing asset says so
    // there instead of disappearing into a detached process.
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    std::string exe = gameExe;
    std::string map = mapPath;
    std::vector<char*> argv{exe.data(), map.data(), nullptr};

    // The parent's environment plus the launch options. Copied rather than set
    // with setenv: the editor keeps running, and mutating its own environment
    // to configure a child is the kind of thing that leaks into the next
    // launch.
    //
    // An inherited variable the caller is setting is dropped rather than
    // duplicated. Which of two identical keys a process sees is not something
    // to rely on, and a stale one silently winning is a setting that appears to
    // do nothing.
    std::vector<std::string> owned = env;
    std::vector<char*> childEnv;
    for (char** it = environ; it && *it; ++it) {
        const char* equals = std::strchr(*it, '=');
        const std::size_t keyLength =
            equals ? std::size_t(equals - *it) + 1 : std::strlen(*it);
        bool overridden = false;
        for (const std::string& entry : owned)
            overridden = overridden ||
                         (entry.size() >= keyLength &&
                          std::strncmp(entry.c_str(), *it, keyLength) == 0);
        if (!overridden)
            childEnv.push_back(*it);
    }
    for (std::string& entry : owned)
        childEnv.push_back(entry.data());
    childEnv.push_back(nullptr);

    pid_t pid = -1;
    const int result = posix_spawn(&pid, gameExe.c_str(), &actions, nullptr,
                                   argv.data(), childEnv.data());
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        handle.error = std::string("posix_spawn failed: ") + std::strerror(result);
        return handle;
    }
    handle.pid = int(pid);
    return handle;
}

bool pollGame(RunHandle& handle, int& exitCode)
{
    if (!handle.running())
        return false;
    int status = 0;
    // WNOHANG: the editor keeps drawing while the game runs; it must never
    // block a frame waiting on it.
    const pid_t result = waitpid(pid_t(handle.pid), &status, WNOHANG);
    if (result == 0)
        return true;
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    handle.pid = -1;
    return false;
}

void stopGame(RunHandle& handle)
{
    if (!handle.running())
        return;
    kill(pid_t(handle.pid), SIGTERM);
    int status = 0;
    waitpid(pid_t(handle.pid), &status, 0);
    handle.pid = -1;
}

} // namespace ed
