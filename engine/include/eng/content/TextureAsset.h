#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// `.rtex` -- the diagram's "TGA Texture -> Compression -> DXT Texture" row.
//
// Two jobs, and they are separable on purpose:
//
//   1. Take the PNG decoder out of the runtime. Every texture in this tree is
//      decoded by stb_image at load; the exported form is already the RGBA the
//      uploader wants, plus its mip chain, which the renderer does not build
//      today at all.
//   2. Compression, when an asset asks for it. BC1/BC3 are here and they work,
//      but `compression = "none"` is the DEFAULT and every shipped texture uses
//      it -- the PSX look is nearest-neighbour pixel art and a block codec
//      visibly changes it. The book's own framing is that this is the
//      artist's choice per asset (1.6.4: "the animator's choice of compression
//      technique and level"), which is exactly where the resource database puts
//      it, rather than a pipeline-wide switch.
//
// The RHI uploads RGBA8. A BC-compressed `.rtex` is therefore decoded on load
// today -- it buys disk and download size, not VRAM. Making it native is a
// Format enum entry, a VkFormat mapping and a block-aligned copy in the RHI;
// the file format is already what that change would want, which is why the
// codec lives here rather than being deferred with it.
namespace eng::content {

inline constexpr char kTextureAssetMagic[8] = {'R', 'A', 'V', 'E',
                                               'N', 'T', 'E', 'X'};
inline constexpr uint16_t kTextureAssetVersion = 1;
inline constexpr const char* kTextureAssetExtension = ".rtex";

enum class TextureFormat : uint8_t {
    Rgba8 = 0, // 4 bytes/texel, exactly what was decoded
    Bc1 = 1,   // DXT1: 8 bytes per 4x4 block, 1-bit alpha
    Bc3 = 2,   // DXT5: 16 bytes per 4x4 block, interpolated alpha
};

std::string_view textureFormatName(TextureFormat);
bool textureFormatFromName(std::string_view, TextureFormat& out);

// Bytes one mip level occupies in a given format. Block formats round the
// dimensions up to a multiple of four, which is why a 6x6 BC1 level is 32 bytes
// and not 18.
size_t textureLevelBytes(TextureFormat, uint32_t width, uint32_t height);

struct TextureLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bytes;
};

struct TextureAsset {
    std::string sourcePath;
    TextureFormat format = TextureFormat::Rgba8;
    // Whether the colour data is sRGB-encoded. Recorded, not applied: the
    // renderer picks the view format, and an asset that lied about this would
    // be a gamma bug nothing points at.
    bool srgb = true;
    // True when the source had any texel with alpha < 255. Drives the BC1-vs-BC3
    // choice, and lets the material pipeline tell an opaque texture from one
    // that needs a blended pass without opening the pixels again.
    bool hasAlpha = false;
    std::vector<TextureLevel> levels; // mip 0 first
};

bool writeTextureAsset(const std::filesystem::path&, const TextureAsset&,
                       std::string& error);

// Reads and, for a block-compressed file, decodes to RGBA8 so the caller always
// receives something the RHI can upload. `out.format` reports what was on disk.
bool readTextureAsset(const std::filesystem::path&, TextureAsset& out,
                      std::string& error);

bool isTextureAsset(const std::filesystem::path&);

// --- the codec, exposed for the exporter and the tests ----------------------

// Encodes a tightly packed RGBA8 image. `hasAlpha` selects BC1 vs BC3 when
// `format` is a block format; passing Rgba8 copies.
std::vector<uint8_t> encodeTextureLevel(TextureFormat, uint32_t width,
                                        uint32_t height,
                                        const uint8_t* rgba);

// The inverse. Returns an empty vector when the byte count does not match the
// dimensions, which is how a truncated level is caught.
std::vector<uint8_t> decodeTextureLevel(TextureFormat, uint32_t width,
                                        uint32_t height,
                                        const std::vector<uint8_t>& bytes);

// Box filter, halving each axis, on RGBA8. Nearest-neighbour art is filtered
// the same way everything else is: a mip chain is a distance LOD, and the
// magnification filter -- which is what makes the look -- is a sampler state,
// not this.
std::vector<uint8_t> downsampleRgba8(uint32_t width, uint32_t height,
                                     const uint8_t* rgba, uint32_t& outWidth,
                                     uint32_t& outHeight);

} // namespace eng::content
