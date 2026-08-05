// The one translation unit in the whole build that compiles stb_image.
//
// Its own CMake target (eng_image_decode) rather than a file inside
// eng_platform, because there are now two consumers: the renderer decodes
// source images at load, and the pipeline's Compression row decodes them at
// build time. When each compiled its own copy, the editor -- which links both
// the engine and eng_acp -- failed with sixty duplicate symbols. One
// definition, linked by whoever needs it.
//
// STBI_ONLY_* only has an effect here; defining it beside a plain #include (as
// two other files do) selects nothing, because the decoders are already
// compiled into this object.
//
// PNG and JPEG for the runtime: the model importer lists .jpg/.jpeg among the
// texture extensions it will copy into the pack, so an imported model with a
// JPEG base colour used to ship a texture the renderer could never decode and
// said so once per material at every load.
//
// TGA and BMP for the pipeline: they are the diagram's own source column for
// the texture row ("TGA Texture -> Compression -> DXT Texture"), and an
// exporter that cannot read what an artist exports is not a pipeline stage.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include <stb_image.h>
