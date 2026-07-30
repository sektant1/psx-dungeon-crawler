#include <eng/render/GifRecorder.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "GifRecorderTests: " << message << '\n';
        std::exit(1);
    }
}

std::optional<eng::RecordingOptions> parse(std::vector<const char*> args)
{
    return eng::GifRecorder::optionsFromArgs(int(args.size()), args.data());
}

} // namespace

int main()
{
    // No --record: nothing to do, even if the tuning switches are present.
    require(!parse({"game", "--record-fps", "30"}).has_value(),
            "tuning switches alone started a recording");

    // Switches are order independent: --record may come after them.
    const auto opts = parse({"game", "--record-fps", "24", "--record-frames",
                             "48", "--record-start", "10", "--record-width",
                             "480", "--record-loops", "0", "--record",
                             "clip.gif", "--record-keep-frames"});
    require(opts.has_value(), "--record was not recognised");
    require(opts->path == "clip.gif", "output path was not parsed");
    require(opts->fps == 24 && opts->frames == 48 && opts->startFrame == 10,
            "clip timing was not parsed");
    require(opts->width == 480 && opts->loops == 0, "framing was not parsed");
    require(opts->keepFrames, "--record-keep-frames was ignored");
    require(opts->frameDirectory() == "clip.gif.frames",
            "frame directory was not derived from the output path");

    // A malformed value keeps the default rather than failing the run.
    const auto bad = parse({"game", "--record", "a.gif", "--record-fps", "x",
                            "--record-frames", "-3"});
    require(bad.has_value(), "a bad value aborted the recording");
    require(bad->fps == eng::RecordingOptions{}.fps &&
                bad->frames == eng::RecordingOptions{}.frames,
            "a bad value overwrote a default");

    // Schedule: nothing before startFrame, exactly `frames` captures after it,
    // numbered from one, and no capture past completion.
    eng::RecordingOptions options;
    options.path = "out.gif";
    options.frameDir = "/tmp/eng-gif-recorder-tests";
    options.startFrame = 3;
    options.frames = 2;
    options.fps = 10;
    std::vector<std::string> written;
    std::vector<std::string> commands;
    eng::GifRecorder::Hooks hooks;
    hooks.writeFrame = [&](const std::string& path) { written.push_back(path); };
    hooks.run = [&](const std::string& command) {
        commands.push_back(command);
        return 0;
    };
    eng::GifRecorder recorder(options, hooks);
    require(recorder.active(), "recorder did not arm");
    for (int frame = 1; frame <= 6; ++frame)
        recorder.afterFrame(frame);
    require(written.size() == 2, "captured the wrong number of frames");
    require(written[0] == options.frameDir + "/frame_00001.png",
            "first frame is not numbered from one");
    require(written[1] == options.frameDir + "/frame_00002.png",
            "frames are not numbered consecutively");
    require(recorder.complete(), "recorder did not complete");

    require(recorder.encode(), "encode reported failure");
    require(commands.size() == 1, "encode did not run exactly one command");
    const std::string& command = commands.front();
    require(command.find("-framerate 10") != std::string::npos,
            "encoder was not given the clip's fps");
    require(command.find("frame_%05d.png") != std::string::npos,
            "encoder was not pointed at the frame sequence");
    require(command.find("palettegen") != std::string::npos &&
                command.find("dither=none") != std::string::npos,
            "encoder lost the flat-colour palette recipe");
    require(command.find("scale=") == std::string::npos,
            "a zero width still scaled the clip");

    // A failing encoder is reported, not swallowed.
    eng::GifRecorder::Hooks failing = hooks;
    failing.run = [](const std::string&) { return 1; };
    eng::GifRecorder broken(options, failing);
    for (int frame = 1; frame <= 6; ++frame)
        broken.afterFrame(frame);
    require(!broken.encode(), "a failing encoder reported success");

    // Nothing captured: nothing to encode.
    eng::RecordingOptions late = options;
    late.startFrame = 100;
    eng::GifRecorder unused(late, hooks);
    unused.afterFrame(1);
    require(!unused.complete() && unused.capturedFrames() == 0,
            "captured a frame before the start frame");
    require(!unused.encode(), "encoded an empty sequence");

    // An explicit width scales with nearest sampling, not bilinear.
    eng::RecordingOptions scaled = options;
    scaled.width = 320;
    eng::GifRecorder wide(scaled, hooks);
    require(wide.encodeCommand().find("scale=320:-1:flags=neighbor") !=
                std::string::npos,
            "width did not produce a nearest-neighbour scale");

    std::cout << "GifRecorderTests OK\n";
}
