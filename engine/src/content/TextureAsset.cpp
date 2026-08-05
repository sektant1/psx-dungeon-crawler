#include <eng/content/TextureAsset.h>

#include <eng/content/AssetFile.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace fs = std::filesystem;

namespace eng::content {
namespace {

constexpr uint32_t kMaxDimension = 16384;
constexpr uint32_t kMaxLevels = 16;

uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void unpackRgb565(uint16_t value, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const uint8_t r5 = static_cast<uint8_t>((value >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((value >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(value & 0x1F);
    // Replicate the high bits into the low ones rather than shifting in zeros,
    // so 31 -> 255 and the white end of a ramp stays white.
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

struct Block {
    std::array<uint8_t, 16 * 4> texels{}; // RGBA, row-major, 4x4
};

// Gathers a 4x4 block, clamping at the edges. A texture whose size is not a
// multiple of four still encodes: the padding repeats the last real texel,
// which is what keeps an edge from bleeding toward black.
Block gatherBlock(uint32_t width, uint32_t height, const uint8_t* rgba,
                  uint32_t blockX, uint32_t blockY)
{
    Block block;
    for (uint32_t y = 0; y < 4; ++y) {
        const uint32_t sy = std::min(blockY + y, height - 1);
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t sx = std::min(blockX + x, width - 1);
            const size_t src = (static_cast<size_t>(sy) * width + sx) * 4;
            const size_t dst = (static_cast<size_t>(y) * 4 + x) * 4;
            std::memcpy(block.texels.data() + dst, rgba + src, 4);
        }
    }
    return block;
}

// Endpoint fit by principal axis: project every texel onto the line through the
// colour extremes and take the two most distant. This is the classic
// "range fit" -- not the best BC1 encoder in existence, but it is deterministic,
// has no tables, and is well within the quality band of an option that ships
// off by default.
void fitEndpoints(const Block& block, bool ignoreAlpha, uint16_t& low,
                  uint16_t& high)
{
    int minChannel[3] = {255, 255, 255};
    int maxChannel[3] = {0, 0, 0};
    bool any = false;
    for (int i = 0; i < 16; ++i) {
        const uint8_t* texel = block.texels.data() + i * 4;
        if (!ignoreAlpha && texel[3] < 128)
            continue; // punch-through texels do not steer the colour line
        any = true;
        for (int c = 0; c < 3; ++c) {
            minChannel[c] = std::min(minChannel[c], int(texel[c]));
            maxChannel[c] = std::max(maxChannel[c], int(texel[c]));
        }
    }
    if (!any) {
        low = high = 0;
        return;
    }
    const uint16_t a = packRgb565(static_cast<uint8_t>(maxChannel[0]),
                                  static_cast<uint8_t>(maxChannel[1]),
                                  static_cast<uint8_t>(maxChannel[2]));
    const uint16_t b = packRgb565(static_cast<uint8_t>(minChannel[0]),
                                  static_cast<uint8_t>(minChannel[1]),
                                  static_cast<uint8_t>(minChannel[2]));
    low = a;
    high = b;
}

int colourDistance(const uint8_t* a, const uint8_t (&b)[3])
{
    const int dr = int(a[0]) - b[0];
    const int dg = int(a[1]) - b[1];
    const int db = int(a[2]) - b[2];
    return dr * dr + dg * dg + db * db;
}

// One BC1 colour block: two RGB565 endpoints and sixteen 2-bit selectors.
// `punchThrough` switches to the 3-colour + transparent mode BC1 defines when
// c0 <= c1, which is what carries 1-bit alpha.
void encodeBc1Block(const Block& block, bool punchThrough, uint8_t* out)
{
    uint16_t c0 = 0, c1 = 0;
    fitEndpoints(block, !punchThrough, c0, c1);

    // Mode is chosen by the ORDER of the endpoints, so the comparison has to be
    // forced rather than hoped for. In opaque mode c0 must be > c1; in
    // punch-through mode it must be <=. Equal endpoints satisfy the second and
    // degrade to a flat block in the first, which is correct either way.
    if (punchThrough) {
        if (c0 > c1)
            std::swap(c0, c1);
    } else {
        if (c0 < c1)
            std::swap(c0, c1);
    }

    uint8_t palette[4][3];
    unpackRgb565(c0, palette[0][0], palette[0][1], palette[0][2]);
    unpackRgb565(c1, palette[1][0], palette[1][1], palette[1][2]);
    if (!punchThrough) {
        for (int c = 0; c < 3; ++c) {
            palette[2][c] =
                static_cast<uint8_t>((2 * palette[0][c] + palette[1][c]) / 3);
            palette[3][c] =
                static_cast<uint8_t>((palette[0][c] + 2 * palette[1][c]) / 3);
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            palette[2][c] =
                static_cast<uint8_t>((palette[0][c] + palette[1][c]) / 2);
            palette[3][c] = 0; // index 3 is transparent black in this mode
        }
    }

    uint32_t selectors = 0;
    for (int i = 0; i < 16; ++i) {
        const uint8_t* texel = block.texels.data() + i * 4;
        uint32_t best = 0;
        if (punchThrough && texel[3] < 128) {
            best = 3;
        } else {
            int bestDistance = colourDistance(texel, palette[0]);
            const int limit = punchThrough ? 3 : 4;
            for (int candidate = 1; candidate < limit; ++candidate) {
                const int distance = colourDistance(texel, palette[candidate]);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = static_cast<uint32_t>(candidate);
                }
            }
        }
        selectors |= best << (i * 2);
    }

    out[0] = static_cast<uint8_t>(c0 & 0xFF);
    out[1] = static_cast<uint8_t>(c0 >> 8);
    out[2] = static_cast<uint8_t>(c1 & 0xFF);
    out[3] = static_cast<uint8_t>(c1 >> 8);
    for (int i = 0; i < 4; ++i)
        out[4 + i] = static_cast<uint8_t>((selectors >> (i * 8)) & 0xFF);
}

// The BC3 alpha block: two endpoints and sixteen 3-bit selectors over an
// 8-value interpolated ramp.
void encodeBc3AlphaBlock(const Block& block, uint8_t* out)
{
    uint8_t low = 255, high = 0;
    for (int i = 0; i < 16; ++i) {
        const uint8_t alpha = block.texels[static_cast<size_t>(i) * 4 + 3];
        low = std::min(low, alpha);
        high = std::max(high, alpha);
    }
    out[0] = high;
    out[1] = low;

    uint8_t ramp[8];
    ramp[0] = high;
    ramp[1] = low;
    if (high > low) {
        for (int i = 0; i < 6; ++i)
            ramp[2 + i] = static_cast<uint8_t>(
                ((6 - i) * high + (1 + i) * low) / 7);
    } else {
        for (int i = 0; i < 6; ++i)
            ramp[2 + i] = high;
    }

    uint64_t selectors = 0;
    for (int i = 0; i < 16; ++i) {
        const int alpha = block.texels[static_cast<size_t>(i) * 4 + 3];
        int best = 0;
        int bestDistance = std::abs(alpha - int(ramp[0]));
        for (int candidate = 1; candidate < 8; ++candidate) {
            const int distance = std::abs(alpha - int(ramp[candidate]));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
        selectors |= static_cast<uint64_t>(best) << (i * 3);
    }
    for (int i = 0; i < 6; ++i)
        out[2 + i] = static_cast<uint8_t>((selectors >> (i * 8)) & 0xFF);
}

void decodeBc1Block(const uint8_t* in, uint8_t* out /* 16 RGBA texels */)
{
    const auto c0 = static_cast<uint16_t>(in[0] | (in[1] << 8));
    const auto c1 = static_cast<uint16_t>(in[2] | (in[3] << 8));
    uint8_t palette[4][4];
    unpackRgb565(c0, palette[0][0], palette[0][1], palette[0][2]);
    unpackRgb565(c1, palette[1][0], palette[1][1], palette[1][2]);
    palette[0][3] = palette[1][3] = 255;
    if (c0 > c1) {
        for (int c = 0; c < 3; ++c) {
            palette[2][c] =
                static_cast<uint8_t>((2 * palette[0][c] + palette[1][c]) / 3);
            palette[3][c] =
                static_cast<uint8_t>((palette[0][c] + 2 * palette[1][c]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int c = 0; c < 3; ++c) {
            palette[2][c] =
                static_cast<uint8_t>((palette[0][c] + palette[1][c]) / 2);
            palette[3][c] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
    uint32_t selectors = 0;
    for (int i = 0; i < 4; ++i)
        selectors |= static_cast<uint32_t>(in[4 + i]) << (i * 8);
    for (int i = 0; i < 16; ++i)
        std::memcpy(out + i * 4, palette[(selectors >> (i * 2)) & 0x3u], 4);
}

void decodeBc3AlphaBlock(const uint8_t* in, uint8_t* out /* 16 RGBA texels */)
{
    const uint8_t high = in[0];
    const uint8_t low = in[1];
    uint8_t ramp[8];
    ramp[0] = high;
    ramp[1] = low;
    if (high > low) {
        for (int i = 0; i < 6; ++i)
            ramp[2 + i] = static_cast<uint8_t>(((6 - i) * high + (1 + i) * low) / 7);
    } else {
        for (int i = 0; i < 4; ++i)
            ramp[2 + i] = static_cast<uint8_t>(((4 - i) * high + (1 + i) * low) / 5);
        ramp[6] = 0;
        ramp[7] = 255;
    }
    uint64_t selectors = 0;
    for (int i = 0; i < 6; ++i)
        selectors |= static_cast<uint64_t>(in[2 + i]) << (i * 8);
    for (int i = 0; i < 16; ++i)
        out[i * 4 + 3] = ramp[(selectors >> (i * 3)) & 0x7u];
}

void scatterBlock(uint32_t width, uint32_t height, uint32_t blockX,
                  uint32_t blockY, const uint8_t* texels, uint8_t* rgba)
{
    for (uint32_t y = 0; y < 4; ++y) {
        const uint32_t dy = blockY + y;
        if (dy >= height)
            break;
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t dx = blockX + x;
            if (dx >= width)
                break;
            std::memcpy(rgba + (static_cast<size_t>(dy) * width + dx) * 4,
                        texels + (static_cast<size_t>(y) * 4 + x) * 4, 4);
        }
    }
}

} // namespace

std::string_view textureFormatName(TextureFormat format)
{
    switch (format) {
    case TextureFormat::Rgba8:
        return "none";
    case TextureFormat::Bc1:
        return "bc1";
    case TextureFormat::Bc3:
        return "bc3";
    }
    return "none";
}

bool textureFormatFromName(std::string_view name, TextureFormat& out)
{
    if (name == "none" || name == "rgba8") {
        out = TextureFormat::Rgba8;
        return true;
    }
    if (name == "bc1" || name == "dxt1") {
        out = TextureFormat::Bc1;
        return true;
    }
    if (name == "bc3" || name == "dxt5") {
        out = TextureFormat::Bc3;
        return true;
    }
    // "auto" is resolved by the exporter, which knows whether the image has
    // alpha; it is not a format and must not decode as one.
    return false;
}

size_t textureLevelBytes(TextureFormat format, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return 0;
    switch (format) {
    case TextureFormat::Rgba8:
        return static_cast<size_t>(width) * height * 4;
    case TextureFormat::Bc1:
    case TextureFormat::Bc3: {
        const size_t blocks = static_cast<size_t>((width + 3) / 4) *
                              ((height + 3) / 4);
        return blocks * (format == TextureFormat::Bc1 ? 8u : 16u);
    }
    }
    return 0;
}

std::vector<uint8_t> encodeTextureLevel(TextureFormat format, uint32_t width,
                                        uint32_t height, const uint8_t* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return {};
    if (format == TextureFormat::Rgba8)
        return {rgba, rgba + static_cast<size_t>(width) * height * 4};

    const uint32_t blocksX = (width + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    const size_t blockBytes = format == TextureFormat::Bc1 ? 8u : 16u;
    std::vector<uint8_t> out(static_cast<size_t>(blocksX) * blocksY * blockBytes);
    size_t at = 0;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const Block block = gatherBlock(width, height, rgba, bx * 4, by * 4);
            if (format == TextureFormat::Bc1) {
                encodeBc1Block(block, /*punchThrough=*/true, out.data() + at);
            } else {
                encodeBc3AlphaBlock(block, out.data() + at);
                encodeBc1Block(block, /*punchThrough=*/false, out.data() + at + 8);
            }
            at += blockBytes;
        }
    }
    return out;
}

std::vector<uint8_t> decodeTextureLevel(TextureFormat format, uint32_t width,
                                        uint32_t height,
                                        const std::vector<uint8_t>& bytes)
{
    if (bytes.size() != textureLevelBytes(format, width, height))
        return {};
    if (format == TextureFormat::Rgba8)
        return bytes;

    const uint32_t blocksX = (width + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    const size_t blockBytes = format == TextureFormat::Bc1 ? 8u : 16u;
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4, 0);
    size_t at = 0;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            uint8_t texels[16 * 4];
            if (format == TextureFormat::Bc1) {
                decodeBc1Block(bytes.data() + at, texels);
            } else {
                decodeBc1Block(bytes.data() + at + 8, texels);
                decodeBc3AlphaBlock(bytes.data() + at, texels);
            }
            scatterBlock(width, height, bx * 4, by * 4, texels, out.data());
            at += blockBytes;
        }
    }
    return out;
}

std::vector<uint8_t> downsampleRgba8(uint32_t width, uint32_t height,
                                     const uint8_t* rgba, uint32_t& outWidth,
                                     uint32_t& outHeight)
{
    outWidth = std::max(1u, width / 2);
    outHeight = std::max(1u, height / 2);
    std::vector<uint8_t> out(static_cast<size_t>(outWidth) * outHeight * 4);
    for (uint32_t y = 0; y < outHeight; ++y) {
        const uint32_t y0 = std::min(y * 2, height - 1);
        const uint32_t y1 = std::min(y * 2 + 1, height - 1);
        for (uint32_t x = 0; x < outWidth; ++x) {
            const uint32_t x0 = std::min(x * 2, width - 1);
            const uint32_t x1 = std::min(x * 2 + 1, width - 1);
            const size_t source[4] = {
                (static_cast<size_t>(y0) * width + x0) * 4,
                (static_cast<size_t>(y0) * width + x1) * 4,
                (static_cast<size_t>(y1) * width + x0) * 4,
                (static_cast<size_t>(y1) * width + x1) * 4};
            uint8_t* destination =
                out.data() + (static_cast<size_t>(y) * outWidth + x) * 4;
            for (int c = 0; c < 4; ++c) {
                uint32_t sum = 0;
                for (size_t offset : source)
                    sum += rgba[offset + static_cast<size_t>(c)];
                destination[c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

bool writeTextureAsset(const fs::path& path, const TextureAsset& asset,
                       std::string& error)
{
    if (asset.levels.empty()) {
        error = "texture has no levels";
        return false;
    }
    io::ByteWriter out;
    out.str(asset.sourcePath);
    out.u8(static_cast<uint8_t>(asset.format));
    out.u8(asset.srgb ? 1u : 0u);
    out.u8(asset.hasAlpha ? 1u : 0u);
    out.u8(0); // reserved
    out.u32(static_cast<uint32_t>(asset.levels.size()));
    for (const TextureLevel& level : asset.levels) {
        out.u32(level.width);
        out.u32(level.height);
        out.u32(static_cast<uint32_t>(level.bytes.size()));
        out.raw(level.bytes.data(), level.bytes.size());
    }
    return writeAssetFile(path, kTextureAssetMagic, kTextureAssetVersion, out,
                          error);
}

bool readTextureAsset(const fs::path& path, TextureAsset& out, std::string& error)
{
    out = {};
    AssetFileBody body;
    if (!readAssetFile(path, kTextureAssetMagic, body, error))
        return false;
    if (body.version != kTextureAssetVersion) {
        error = "rtex version " + std::to_string(body.version) +
                ", this build reads " + std::to_string(kTextureAssetVersion);
        return false;
    }

    io::ByteReader in = body.reader();
    out.sourcePath = in.str();
    const auto format = static_cast<TextureFormat>(in.u8());
    out.srgb = in.u8() != 0;
    out.hasAlpha = in.u8() != 0;
    in.u8();
    if (format != TextureFormat::Rgba8 && format != TextureFormat::Bc1 &&
        format != TextureFormat::Bc3) {
        error = "unknown texture format";
        return false;
    }

    const uint32_t levelCount = in.u32();
    if (!in.ok() || levelCount == 0 || levelCount > kMaxLevels) {
        error = "bad mip level count";
        return false;
    }
    out.format = format;
    out.levels.resize(levelCount);
    for (TextureLevel& level : out.levels) {
        level.width = in.u32();
        level.height = in.u32();
        const uint32_t byteCount = in.u32();
        if (!in.ok() || level.width == 0 || level.height == 0 ||
            level.width > kMaxDimension || level.height > kMaxDimension ||
            byteCount != textureLevelBytes(format, level.width, level.height)) {
            error = "mip level does not match its declared size";
            return false;
        }
        level.bytes.resize(byteCount);
        if (!in.raw(level.bytes.data(), byteCount)) {
            error = "truncated mip level";
            return false;
        }
    }
    if (!in.ok()) {
        error = "truncated rtex payload";
        return false;
    }

    // Decoded here, not by the caller: every consumer wants RGBA8, and one of
    // them forgetting to check the format is a texture that renders as noise.
    if (format != TextureFormat::Rgba8) {
        for (TextureLevel& level : out.levels) {
            level.bytes =
                decodeTextureLevel(format, level.width, level.height, level.bytes);
            if (level.bytes.empty()) {
                error = "cannot decode compressed mip level";
                return false;
            }
        }
    }
    return true;
}

bool isTextureAsset(const fs::path& path)
{
    return assetFileMatches(path, kTextureAssetMagic);
}

} // namespace eng::content
