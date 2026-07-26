#pragma once

#include <functional>
#include <string>

namespace eng {

// Optional one-frame RenderDoc trigger. The production API is discovered from
// an injected RenderDoc module; Api is public so the state machine is testable
// without RenderDoc or a graphics context.
class FrameCapture
{
public:
    struct Api {
        std::function<void(const std::string&)> setCapturePath;
        std::function<bool()> start;
        std::function<bool()> end;
    };

    FrameCapture() = default;
    FrameCapture(int frame, std::string pathTemplate, Api api);

    static FrameCapture fromEnvironment();

    void beforeFrame(int frame);
    void afterFrame(int frame);

    bool requested() const { return mFrame > 0; }
    bool completed() const { return mCompleted; }
    bool failed() const { return mFailed; }
    bool terminal() const { return mCompleted || mFailed; }
    int requestedFrame() const { return mFrame; }

private:
    int mFrame = 0;
    std::string mPathTemplate;
    Api mApi;
    bool mStarted = false;
    bool mCompleted = false;
    bool mFailed = false;
};

} // namespace eng
