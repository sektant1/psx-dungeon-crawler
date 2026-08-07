#pragma once

#include <eng/rhi/Device.h>

namespace eng::rhi::gl {

// OpenGL backend -- not written yet. See engine/src/rhi/README.md.
//
// Implement eng::rhi::Device here, add this file's .cpp to the eng_rhi target,
// and return the device from createDevice in ../Registry.cpp. Nothing above
// the RHI needs to change when you do.
//
// Notes that will save time, from what the shipping Vulkan path relies on:
//  - The PSX chain renders at 1/3 of the window and upscales with nearest
//    filtering. Whatever you do about filtering elsewhere, that has to stay
//    exact or the look changes.
//  - psx_lighting.glsl binds 16 light slots; report that in capabilities().
//  - The bloom targets are RGBA16Float. Values above 1 are deliberate.
std::unique_ptr<Device> createDevice(const DeviceDesc&);

} // namespace eng::rhi::gl
