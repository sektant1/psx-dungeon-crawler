#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h>

namespace eng::ecs {

// A light to attach when SceneSync first sees this entity. Callers set only
// `desc`; `handle` is filled in by the sync and is how anything later retints
// or moves the light.
struct LightRef {
    LightDesc desc;
    LightHandle handle;
};

} // namespace eng::ecs
