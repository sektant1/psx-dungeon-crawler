#pragma once
#include <entt/entt.hpp>
#include <vector>
namespace editor {
class Selection {
public:
    void set(entt::entity e){ mItems.clear(); if(e!=entt::null) mItems.push_back(e); }
    void add(entt::entity e){ if(e!=entt::null) mItems.push_back(e); }
    void clear(){ mItems.clear(); }
    bool empty() const { return mItems.empty(); }
    entt::entity primary() const { return mItems.empty()?entt::null:mItems.front(); }
    const std::vector<entt::entity>& items() const { return mItems; }
private:
    std::vector<entt::entity> mItems;
};
}
