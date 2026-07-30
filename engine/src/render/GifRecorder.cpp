#include <eng/render/GifRecorder.h>

#include <eng/Log.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace eng {

namespace {

int positiveInt(const char* text, int fallback)
{
    if (!text || !*text)
        return fallback;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value <= 0 || value > INT_MAX)
        return fallback;
    return static_cast<int>(value);
}

int nonNegativeInt(const char* text, int fallback)
{
    if (text && text[0] == '0' && text[1] == '\0')
        return 0;
    return positiveInt(text, fallback);
}

// The recorder shells out, so every interpolated value is quoted and any quote
// in it is dropped rather than escaped: a path is not a place to smuggle shell
// syntax through.
std::string shellQuoted(const std::string& value)
{
    std::string out = "'";
    for (const char c : value)
        if (c != '\'')
            out.push_back(c);
    out.push_back('\'');
    return out;
}

} // namespace

std::string RecordingOptions::frameDirectory() const
{
    return frameDir.empty() ? path + ".frames" : frameDir;
}

GifRecorder::GifRecorder(RecordingOptions options, Hooks hooks)
    : mOptions(std::move(options)), mHooks(std::move(hooks))
{
    mActive = mOptions.frames > 0 && mHooks.writeFrame != nullptr;
}

std::optional<RecordingOptions> GifRecorder::optionsFromArgs(
    int argc, const char* const* argv)
{
    // Every switch is read in one pass and the result is only handed back if
    // --record itself appeared, so argument order does not matter.
    RecordingOptions parsed;
    bool requested = false;
    const auto value = [&](int i) -> const char* {
        return i + 1 < argc ? argv[i + 1] : nullptr;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--record") {
            if (!value(i))
                continue;
            parsed.path = value(i);
            requested = true;
        } else if (arg == "--record-frames") {
            parsed.frames = positiveInt(value(i), parsed.frames);
        } else if (arg == "--record-fps") {
            parsed.fps = positiveInt(value(i), parsed.fps);
        } else if (arg == "--record-start") {
            parsed.startFrame = positiveInt(value(i), parsed.startFrame);
        } else if (arg == "--record-width") {
            parsed.width = nonNegativeInt(value(i), parsed.width);
        } else if (arg == "--record-loops") {
            parsed.loops = nonNegativeInt(value(i), parsed.loops);
        } else if (arg == "--record-frame-dir") {
            if (value(i))
                parsed.frameDir = value(i);
        } else if (arg == "--record-keep-frames") {
            parsed.keepFrames = true;
        }
    }
    if (!requested)
        return std::nullopt;
    return parsed;
}

std::string GifRecorder::framePath(int index) const
{
    char name[32];
    std::snprintf(name, sizeof(name), "/frame_%05d.png", index);
    return mOptions.frameDirectory() + name;
}

void GifRecorder::afterFrame(int frame)
{
    if (!mActive || mComplete || frame < mOptions.startFrame)
        return;
    if (mCaptured == 0) {
        std::error_code error;
        std::filesystem::create_directories(mOptions.frameDirectory(), error);
        if (error) {
            log::error("GifRecorder: cannot create %s: %s",
                       mOptions.frameDirectory().c_str(),
                       error.message().c_str());
            mActive = false;
            return;
        }
    }
    mHooks.writeFrame(framePath(mCaptured + 1));
    ++mCaptured;
    if (mCaptured >= mOptions.frames)
        mComplete = true;
}

std::string GifRecorder::encodeCommand() const
{
    // palettegen/paletteuse instead of ffmpeg's default quantiser: a 256-colour
    // palette fitted to the clip is what keeps flat retro colour flat. Nearest
    // scaling and no dithering for the same reason -- bilinear and error
    // diffusion both turn pixel art into mush.
    std::string filter = "[0:v] ";
    if (mOptions.width > 0)
        filter += "scale=" + std::to_string(mOptions.width) +
                  ":-1:flags=neighbor,";
    filter += "split [a][b];[a] palettegen=stats_mode=diff [p];"
              "[b][p] paletteuse=dither=none";
    return "ffmpeg -y -loglevel error -framerate " +
           std::to_string(mOptions.fps) + " -i " +
           shellQuoted(mOptions.frameDirectory() + "/frame_%05d.png") +
           " -filter_complex " + shellQuoted(filter) + " -loop " +
           std::to_string(mOptions.loops) + " " + shellQuoted(mOptions.path);
}

bool GifRecorder::encode()
{
    if (mCaptured <= 0 || !mHooks.run)
        return false;
    const std::string command = encodeCommand();
    const int status = mHooks.run(command);
    if (status != 0) {
        log::error("GifRecorder: encoder failed (%d). Frames kept in %s. "
                   "Command: %s",
                   status, mOptions.frameDirectory().c_str(), command.c_str());
        return false;
    }
    log::info("GifRecorder: wrote %s (%d frames at %d fps)",
              mOptions.path.c_str(), mCaptured, mOptions.fps);
    if (!mOptions.keepFrames) {
        std::error_code error;
        std::filesystem::remove_all(mOptions.frameDirectory(), error);
    }
    return true;
}

} // namespace eng
