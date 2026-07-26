#include <eng/render/FrameCapture.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    int paths = 0;
    int starts = 0;
    int ends = 0;
    eng::FrameCapture::Api api;
    api.setCapturePath = [&](const std::string& path) {
        ++paths;
        require(path == "/tmp/game-capture", "capture path must be forwarded");
    };
    api.start = [&] {
        ++starts;
        return true;
    };
    api.end = [&] {
        ++ends;
        return true;
    };

    eng::FrameCapture capture(7, "/tmp/game-capture", std::move(api));
    require(capture.requested(), "positive frame must request capture");
    for (int frame = 1; frame < 7; ++frame) {
        capture.beforeFrame(frame);
        capture.afterFrame(frame);
    }
    require(starts == 0 && ends == 0, "capture must wait for requested frame");
    capture.beforeFrame(7);
    require(paths == 1 && starts == 1, "requested frame must start once");
    capture.afterFrame(7);
    require(ends == 1 && capture.completed(), "requested frame must end once");
    require(capture.terminal(), "completed capture must be terminal");
    capture.beforeFrame(7);
    capture.afterFrame(7);
    require(starts == 1 && ends == 1, "capture must never repeat");

    eng::FrameCapture unavailable(3, "", {});
    require(unavailable.requested(), "missing API does not cancel request");
    unavailable.beforeFrame(3);
    require(unavailable.failed(), "missing API must fail requested capture");
    require(unavailable.terminal(), "failed capture must be terminal");

    eng::FrameCapture disabled(0, "", {});
    disabled.beforeFrame(1);
    disabled.afterFrame(1);
    require(!disabled.requested() && !disabled.failed(),
            "non-positive frame must disable capture");

    std::cout << "FrameCaptureTests: all tests passed\n";
    return 0;
}
