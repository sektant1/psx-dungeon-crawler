#pragma once

#include <eng/rhi/Device.h>

namespace eng::rhi::vulkan {

// This is the backend the engine ships on: RenderCore creates it through
// rhi::createDevice and draws every frame with it. samples/rhi-vulkan-smoke
// exercises it in isolation.
std::unique_ptr<Device> createDevice(const DeviceDesc&);

} // namespace eng::rhi::vulkan
