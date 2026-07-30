#pragma once

#include <eng/rhi/Device.h>

namespace eng::rhi::null {

// Records and validates; draws nothing. Two jobs: let headless tests exercise
// code that needs a device, and define by example what a backend must accept.
// It is deliberately strict -- stale handles, unbalanced frames and passes,
// oversized push constants and out-of-range writes are all reported here
// rather than becoming a black screen in a real backend.
std::unique_ptr<Device> createDevice(const DeviceDesc&);

} // namespace eng::rhi::null
