#pragma once

#include <eng/Loading.h>

namespace eng {

// How much of the renderer to touch before the first playable frame.
struct WarmupOptions {
    // Load every material and resolve its best technique. This is what turns a
    // GLSL compile error into a startup log line instead of a blank surface the
    // first time that material is drawn, and it moves the compile hitch into
    // the loading screen where it costs nothing.
    bool materials = true;
    // Load the textures the loaded materials reference. Uploads are the other
    // half of the first-frame hitch.
    bool textures = true;
    // Load every mesh already declared in the resource groups (vertex/index
    // buffer upload).
    bool meshes = false;
    // Resources touched per slice. Small enough that one slice stays under a
    // frame budget, big enough that the loop is not all overhead.
    int chunk = 12;
};

// Appends warmup steps to `plan`. Resumable and chunked, so the loading screen
// keeps animating while shaders compile. Safe to call with no scene built.
void addRenderWarmup(LoadPlan& plan, const WarmupOptions& options = {});

} // namespace eng
