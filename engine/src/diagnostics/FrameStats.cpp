#include <eng/FrameStats.h>
#include <eng/Log.h>

#include <string>

namespace eng {

FrameStats::FrameStats(std::vector<std::string> phaseNames)
    : mNames(std::move(phaseNames)), mPhaseMs(mNames.size(), 0.0f)
{
    mNamePtrs.reserve(mNames.size());
    for (const std::string& n : mNames)
        mNamePtrs.push_back(n.c_str()); // mNames never reallocates after this
}

void FrameStats::beginFrame()
{
    for (float& m : mPhaseMs)
        m = 0.0f;
}

void FrameStats::endFrame(float totalMs)
{
    mFrameHist[mHistHead] = totalMs;
    mHistHead = (mHistHead + 1) % kHistory;
}

void FrameStats::addPhase(int phase, float ms)
{
    if (phase >= 0 && size_t(phase) < mPhaseMs.size())
        mPhaseMs[size_t(phase)] += ms;
}

void FrameStats::setPhase(int phase, float ms)
{
    if (phase >= 0 && size_t(phase) < mPhaseMs.size())
        mPhaseMs[size_t(phase)] = ms;
}

float FrameStats::phaseMs(int phase) const
{
    if (phase >= 0 && size_t(phase) < mPhaseMs.size())
        return mPhaseMs[size_t(phase)];
    return 0.0f;
}

FrameStatsView FrameStats::view() const
{
    FrameStatsView v;
    v.frameHist = mFrameHist;
    v.histCount = kHistory;
    v.histHead = mHistHead;
    v.phaseNames = mNamePtrs.empty() ? nullptr : mNamePtrs.data();
    v.phaseMs = mPhaseMs.empty() ? nullptr : mPhaseMs.data();
    v.phaseCount = int(mPhaseMs.size());
    return v;
}

void FrameStats::logSummary() const
{
    float total = 0.0f;
    for (float m : mPhaseMs)
        total += m;
    std::string line;
    for (size_t i = 0; i < mPhaseMs.size(); ++i) {
        line += ' ';
        line += mNames[i];
        line += ' ';
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", double(mPhaseMs[i]));
        line += buf;
    }
    log::info("Frame %.2f ms (%.0f fps) |%s", double(total),
              total > 0.0f ? 1000.0 / double(total) : 0.0, line.c_str());
}

void FrameStats::logSummaryEvery(int everyN)
{
    if (everyN <= 0)
        return;
    if (++mLogTick % everyN == 0)
        logSummary();
}

} // namespace eng
