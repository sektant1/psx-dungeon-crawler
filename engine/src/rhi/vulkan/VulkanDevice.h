#pragma once

#include <eng/rhi/Device.h>

namespace eng::rhi::vulkan {

// Vulkan backend -- not written yet. See engine/src/rhi/README.md.
//
// Implement eng::rhi::Device here, add this file's .cpp to the eng_rhi target,
// and return the device from createDevice in ../Registry.cpp.
//
// The contract was shaped to leave the Vulkan-specific decisions to you rather
// than pre-empting them:
//  - RenderPassDesc is described per frame, not cached, so you can build
//    VkRenderPass/dynamic-rendering objects however you prefer.
//  - pushConstants is a small per-draw blob; map it to real push constants.
//  - Handles are generational so you are free to recreate everything on
//    device loss or swapchain recreation without invalidating callers.
//  - beginFrame returning false is the out-of-date-swapchain path.
std::unique_ptr<Device> createDevice(const DeviceDesc&);

} // namespace eng::rhi::vulkan
