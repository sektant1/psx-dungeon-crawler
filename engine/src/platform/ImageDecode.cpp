// The one translation unit that compiles stb_image. STBI_ONLY_* only has an
// effect here -- defining it beside a plain #include (as two other files do)
// selects nothing, because the decoders are already compiled into this object.
//
// PNG and JPEG. PNG alone was the original choice, but the model importer lists
// .jpg/.jpeg among the texture extensions it will copy into the pack, so an
// imported model with a JPEG base colour shipped a texture the renderer could
// never decode and said so once per material at every load. Either the importer
// had to reject them or this had to read them; reading them is the one that
// does not make an ordinary source asset an error.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>
