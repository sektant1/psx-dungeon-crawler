#include <editor/app/SceneTabs.h>

#include <filesystem>
#include <utility>

namespace ed {

std::string sceneTabName(const SceneTab& tab)
{
    if (tab.path.empty())
        return "[unsaved]";
    return std::filesystem::path(tab.path).filename().string();
}

std::string sceneTabTooltip(const SceneTab& tab)
{
    if (tab.path.empty())
        return "never saved -- Ctrl+S will ask where";
    return tab.path;
}

SceneTabs::SceneTabs()
{
    mTabs.emplace_back().uid = nextUid();
}

std::size_t SceneTabs::indexOfPath(const std::string& path) const
{
    if (path.empty())
        return mTabs.size(); // an unsaved tab is never "the same file" as one
    for (std::size_t i = 0; i < mTabs.size(); ++i)
        if (mTabs[i].path == path)
            return i;
    return mTabs.size();
}

std::size_t SceneTabs::open(SceneTab tab)
{
    tab.uid = nextUid();
    mTabs.push_back(std::move(tab));
    mActive = mTabs.size() - 1;
    return mActive;
}

void SceneTabs::activate(std::size_t index)
{
    if (index >= mTabs.size())
        return;
    mActive = index;
}

bool SceneTabs::close(std::size_t index)
{
    if (index >= mTabs.size())
        return false;

    if (mTabs.size() == 1) {
        mTabs[0] = SceneTab{};
        mTabs[0].uid = nextUid();
        mActive = 0;
        return true;
    }

    const bool closingActive = mActive == index;
    mTabs.erase(mTabs.begin() + std::ptrdiff_t(index));
    // Godot's rule, and the one that surprises nobody: closing the tab you are
    // on lands you on its left neighbour, and closing one to the left of it
    // must not move you at all.
    //
    // Closing the active one needs the same decrement as closing one before it.
    // Without it `mActive` kept its index, which after the erase is the tab to
    // the RIGHT -- so the documented rule only held for the last tab, which was
    // the one case the trailing clamp happened to cover.
    if (mActive > index || (closingActive && mActive > 0))
        --mActive;
    if (mActive >= mTabs.size())
        mActive = mTabs.size() - 1;
    return false;
}

bool SceneTabs::anyDirty() const
{
    for (const SceneTab& tab : mTabs)
        if (tab.dirty)
            return true;
    return false;
}

} // namespace ed
