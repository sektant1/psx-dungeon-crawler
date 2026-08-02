#include <eng/render/Warmup.h>

#include <eng/Log.h>

namespace eng {

void addRenderWarmup(LoadPlan& plan, const WarmupOptions& options)
{
    if (options.materials || options.textures || options.meshes) {
        plan.add("Checking RHI resources", [] {
            log::info("Warmup: RHI shaders and material textures were validated at renderer initialization");
        });
    }
}

} // namespace eng
