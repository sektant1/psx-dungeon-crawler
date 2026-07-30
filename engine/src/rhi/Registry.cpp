#include <eng/rhi/Device.h>

#include <eng/Log.h>

#include "gl/GLDevice.h"
#include "null/NullDevice.h"
#include "vulkan/VulkanDevice.h"

namespace eng::rhi {

const char* backendName(BackendKind kind)
{
    switch (kind) {
    case BackendKind::Null:   return "null";
    case BackendKind::OpenGL: return "opengl";
    case BackendKind::Vulkan: return "vulkan";
    }
    return "unknown";
}

bool backendKindFromName(const std::string& name, BackendKind& out)
{
    if (name == "null")   { out = BackendKind::Null;   return true; }
    if (name == "opengl" || name == "gl") { out = BackendKind::OpenGL; return true; }
    if (name == "vulkan") { out = BackendKind::Vulkan; return true; }
    return false;
}

std::unique_ptr<Device> createDevice(BackendKind kind, const DeviceDesc& desc)
{
    switch (kind) {
    case BackendKind::Null:   return null::createDevice(desc);
    case BackendKind::OpenGL: return gl::createDevice(desc);
    case BackendKind::Vulkan: return vulkan::createDevice(desc);
    }
    log::error("rhi: unknown backend kind %d", int(kind));
    return nullptr;
}

} // namespace eng::rhi
