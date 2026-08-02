#pragma once

#include <eng/rhi/Device.h>

namespace eng::rhi::vulkan {

// The implementation remains private and opt-in. It deliberately has no route
// into the live OGRE renderer; samples/rhi-vulkan-smoke is its only windowed
// caller until the backend has completed validation and visual qualification.
std::unique_ptr<Device> createDevice(const DeviceDesc&);

} // namespace eng::rhi::vulkan
