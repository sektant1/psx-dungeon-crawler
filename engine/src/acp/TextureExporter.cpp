// The diagram's "Photoshop / Z-Brush / Substance -> TGA Texture -> Compression
// -> DXT Texture" row.
//
// The source column is whatever stb can decode -- .png here, .tga and .bmp
// because the diagram names TGA and imported models bring both. The Compression
// box is real: BC1 and BC3 are implemented in eng/content/TextureAsset.h and
// selected per asset from the resource database. It is off by default, because
// the shipped look is nearest-neighbour pixel art and a block codec visibly
// changes it -- the pipeline's job is to make the choice available and record
// it, not to make it for the artist.
//
// Decoding goes through eng_image_decode, the single translation unit in the
// build that compiles stb_image. The editor links both the engine and this
// library, so a second copy here is sixty duplicate symbols at link time.

#include "Exporters.h"

#include <eng/content/TextureAsset.h>

#include <stb_image.h>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace eng::acp {
namespace {

// What an asset asked Compression to do. Resolvable without opening the file
// except for "auto", which needs to know whether there is alpha -- and both of
// its answers are block formats, so the output NAME is still decidable here.
enum class Requested { Passthrough, Rgba8, Bc1, Bc3, Auto };

Requested requestedCompression(const content::Settings& settings,
                               std::string& raw)
{
    raw = content::settingString(settings, "compression", "none");
    if (raw == "none")
        return Requested::Passthrough;
    if (raw == "auto")
        return Requested::Auto;
    if (raw == "rgba8")
        return Requested::Rgba8;
    if (raw == "bc1" || raw == "dxt1")
        return Requested::Bc1;
    if (raw == "bc3" || raw == "dxt5")
        return Requested::Bc3;
    return Requested::Passthrough;
}

class TextureExporter final : public Exporter {
public:
    std::string_view name() const override { return "Compression"; }
    AssetType type() const override { return AssetType::Texture; }
    // 1: initial .rtex (RGBA8/BC1/BC3, box-filtered mip chain).
    uint32_t version() const override { return 1; }

    // `compression = "none"` publishes the source image unchanged.
    //
    // The obvious alternative -- always write a .rtex, uncompressed, to take
    // the PNG decoder out of the runtime -- was measured and is a bad trade for
    // this content: 6.8 MB of PSX pixel art expands to 502 MB as raw RGBA8,
    // because flat indexed colour is what PNG is best at. Reading 74x the bytes
    // costs more than the decode it saves, and start-up got *slower*. So the
    // conditioned form is produced when it earns its place -- a block format,
    // which is smaller than the PNG and is what the GPU wants -- and not
    // otherwise.
    std::string outputFor(const Record& record) const override
    {
        std::string raw;
        if (requestedCompression(record.import, raw) == Requested::Passthrough)
            return record.logical;
        return Exporter::outputFor(record);
    }

    ExportResult run(const ExportContext& context) const override
    {
        ExportResult result;

        std::string raw;
        const Requested requested =
            requestedCompression(context.record->import, raw);
        if (requested == Requested::Passthrough) {
            if (raw != "none")
                result.warnings.push_back("unknown compression '" + raw +
                                          "', publishing the source image");
            std::error_code ec;
            fs::copy_file(context.sourcePath, context.outputPath,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                result.error = "cannot copy: " + ec.message();
                return result;
            }
            result.ok = true;
            return result;
        }

        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(context.sourcePath.string().c_str(), &width,
                                    &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            result.error = std::string("cannot decode: ") +
                           (stbi_failure_reason() ? stbi_failure_reason()
                                                  : "unknown");
            return result;
        }
        const size_t texels = static_cast<size_t>(width) * height;
        std::vector<uint8_t> rgba(pixels, pixels + texels * 4);
        stbi_image_free(pixels);

        content::TextureAsset asset;
        asset.sourcePath = context.record->logical;
        asset.srgb = content::settingBool(context.record->import, "srgb", true);
        for (size_t i = 0; i < texels; ++i) {
            if (rgba[i * 4 + 3] != 255) {
                asset.hasAlpha = true;
                break;
            }
        }

        switch (requested) {
        case Requested::Auto:
            // The one setting the exporter resolves rather than obeys, because
            // the answer depends on the pixels: alpha needs BC3's interpolated
            // alpha block, opaque art does not.
            asset.format = asset.hasAlpha ? content::TextureFormat::Bc3
                                          : content::TextureFormat::Bc1;
            break;
        case Requested::Bc1:
            asset.format = content::TextureFormat::Bc1;
            break;
        case Requested::Bc3:
            asset.format = content::TextureFormat::Bc3;
            break;
        case Requested::Rgba8:
        case Requested::Passthrough:
            asset.format = content::TextureFormat::Rgba8;
            break;
        }

        const auto maxSize = static_cast<uint32_t>(
            content::settingInteger(context.record->import, "max_size", 2048));
        if (maxSize > 0 && (static_cast<uint32_t>(width) > maxSize ||
                            static_cast<uint32_t>(height) > maxSize)) {
            // Reported, never applied. Resizing a texture behind the artist's
            // back is how a UI atlas silently stops lining up; the budget is
            // something to be told about and fixed at the source.
            result.warnings.push_back(
                std::to_string(width) + "x" + std::to_string(height) +
                " exceeds max_size " + std::to_string(maxSize));
        }

        // Off unless asked. The RHI's TextureDesc uploads one level, so a mip
        // chain is 33% more bytes for something nothing reads yet; the format
        // carries it so that turning it on later is a renderer change and not a
        // re-cook of the tree.
        const bool mips =
            content::settingBool(context.record->import, "generate_mips", false);

        auto emit = [&](uint32_t levelWidth, uint32_t levelHeight,
                        const std::vector<uint8_t>& levelRgba) {
            content::TextureLevel level;
            level.width = levelWidth;
            level.height = levelHeight;
            level.bytes = content::encodeTextureLevel(asset.format, levelWidth,
                                                      levelHeight,
                                                      levelRgba.data());
            asset.levels.push_back(std::move(level));
        };

        auto levelWidth = static_cast<uint32_t>(width);
        auto levelHeight = static_cast<uint32_t>(height);
        emit(levelWidth, levelHeight, rgba);
        while (mips && (levelWidth > 1 || levelHeight > 1)) {
            uint32_t nextWidth = 0, nextHeight = 0;
            rgba = content::downsampleRgba8(levelWidth, levelHeight, rgba.data(),
                                            nextWidth, nextHeight);
            levelWidth = nextWidth;
            levelHeight = nextHeight;
            emit(levelWidth, levelHeight, rgba);
        }

        if (!content::writeTextureAsset(context.outputPath, asset, result.error))
            return result;
        result.ok = true;
        return result;
    }
};

} // namespace

std::unique_ptr<Exporter> makeTextureExporter()
{
    return std::make_unique<TextureExporter>();
}

} // namespace eng::acp
