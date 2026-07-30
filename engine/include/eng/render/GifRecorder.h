#pragma once

#include <functional>
#include <optional>
#include <string>

namespace eng {

// What to record and how to encode it. All of it comes off the command line, so
// a clip can be re-shot with different framing without a rebuild.
struct RecordingOptions {
    std::string path = "recording.gif";
    int startFrame = 60; // warm-up frames dropped before the first capture
    int frames = 120;    // captured frames, one per rendered frame
    int fps = 20;        // playback rate; also pins the sim's fixed timestep
    int width = 0;       // 0 keeps the render resolution
    int loops = 0;       // 0 loops forever
    bool keepFrames = false;
    // Where the intermediate PNGs go. Empty derives "<path>.frames".
    std::string frameDir;

    std::string frameDirectory() const;
};

// Records a fixed-length PNG sequence off the render loop and hands it to an
// external encoder. Both the frame write and the encoder invocation are
// injected, so the schedule and the command line are testable without a
// graphics context or ffmpeg on the machine.
class GifRecorder
{
public:
    struct Hooks {
        std::function<void(const std::string& path)> writeFrame;
        std::function<int(const std::string& command)> run;
    };

    GifRecorder() = default;
    GifRecorder(RecordingOptions options, Hooks hooks);

    // Parses --record and its --record-* companions. Returns nothing when
    // --record is absent; a malformed value falls back to that field's default
    // rather than failing the run.
    static std::optional<RecordingOptions> optionsFromArgs(
        int argc, const char* const* argv);

    bool active() const { return mActive; }
    // Call once per rendered frame, with 1 for the first frame.
    void afterFrame(int frame);
    bool complete() const { return mComplete; }
    int capturedFrames() const { return mCaptured; }

    std::string framePath(int index) const;
    std::string encodeCommand() const;
    // Encodes the captured sequence. False if the encoder reported failure or
    // nothing was captured.
    bool encode();

    const RecordingOptions& options() const { return mOptions; }

private:
    RecordingOptions mOptions;
    Hooks mHooks;
    bool mActive = false;
    bool mComplete = false;
    int mCaptured = 0;
};

} // namespace eng
