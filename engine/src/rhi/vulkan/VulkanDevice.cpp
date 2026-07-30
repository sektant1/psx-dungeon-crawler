#include "VulkanDevice.h"

#include <eng/Log.h>

namespace eng::rhi::vulkan {

std::unique_ptr<Device> createDevice(const DeviceDesc&)
{
    log::error("rhi: the Vulkan backend is a skeleton -- see "
               "engine/src/rhi/README.md");
    return nullptr;
}

} // namespace eng::rhi::vulkan
