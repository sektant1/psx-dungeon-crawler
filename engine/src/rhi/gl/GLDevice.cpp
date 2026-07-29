#include "GLDevice.h"

#include <eng/Log.h>

namespace eng::rhi::gl {

std::unique_ptr<Device> createDevice(const DeviceDesc&)
{
    // Deliberately fails rather than returning a device that draws nothing:
    // a backend that silently renders a black screen is worse than one that
    // says it does not exist.
    log::error("rhi: the OpenGL backend is a skeleton -- see "
               "engine/src/rhi/README.md");
    return nullptr;
}

} // namespace eng::rhi::gl
