#include <eng/render/FrameCapture.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <utility>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#if __has_include(<renderdoc_app.h>)
#include <renderdoc_app.h>
#define ENG_HAS_RENDERDOC_APP_HEADER 1
#else
#define ENG_HAS_RENDERDOC_APP_HEADER 0
#endif

namespace eng {

namespace {

int positiveFrame(const char* text)
{
    if (!text || !*text)
        return 0;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value <= 0 ||
        value > INT_MAX)
        return 0;
    return static_cast<int>(value);
}

FrameCapture::Api discoverApi()
{
    FrameCapture::Api result;
#if ENG_HAS_RENDERDOC_APP_HEADER && (defined(__linux__) || defined(__APPLE__))
    using GetApi = int (*)(RENDERDOC_Version, void**);
    auto* getApi =
        reinterpret_cast<GetApi>(dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI"));
    if (!getApi)
        return result;

    RENDERDOC_API_1_6_0* api = nullptr;
    if (getApi(eRENDERDOC_API_Version_1_6_0,
               reinterpret_cast<void**>(&api)) != 1 ||
        !api)
        return result;

    result.setCapturePath = [api](const std::string& path) {
        api->SetCaptureFilePathTemplate(path.c_str());
    };
    result.start = [api] {
        api->StartFrameCapture(nullptr, nullptr);
        return true;
    };
    result.end = [api] {
        return api->EndFrameCapture(nullptr, nullptr) == 1;
    };
#endif
    return result;
}

} // namespace

FrameCapture::FrameCapture(int frame, std::string pathTemplate, Api api)
    : mFrame(frame > 0 ? frame : 0),
      mPathTemplate(std::move(pathTemplate)),
      mApi(std::move(api))
{
}

FrameCapture FrameCapture::fromEnvironment()
{
    const int frame = positiveFrame(std::getenv("RAVEN_RENDERDOC_FRAME"));
    const char* path = std::getenv("RAVEN_RENDERDOC_CAPTURE");
    return FrameCapture(frame, path ? path : "", discoverApi());
}

void FrameCapture::beforeFrame(int frame)
{
    if (!requested() || mStarted || mCompleted || mFailed || frame != mFrame)
        return;
    if (!mApi.start || !mApi.end) {
        mFailed = true;
        return;
    }
    if (!mPathTemplate.empty() && mApi.setCapturePath)
        mApi.setCapturePath(mPathTemplate);
    mStarted = mApi.start();
    mFailed = !mStarted;
}

void FrameCapture::afterFrame(int frame)
{
    if (!mStarted || mCompleted || mFailed || frame != mFrame)
        return;
    mCompleted = mApi.end();
    mFailed = !mCompleted;
}

} // namespace eng
