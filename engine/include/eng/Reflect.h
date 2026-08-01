#pragma once
#include <eng/StringId.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace eng {

// Field reflection: the description a struct carries of its own data, so one
// table drives serialisation, the inspector, the add/remove menu and -- for a
// shader block -- the uniform push, instead of four hand-written copies.
//
// Lives at the top level rather than under eng/ecs because it describes *data*,
// not entities. The renderer needs it to push a component's fields as shader
// uniforms (see eng/ShaderBlock.h), and eng/ecs sits a layer above the renderer.
//
// *Game Engine Architecture* (4th ed.) lists "RTTI / Reflection & Serialization"
// as a core support system for exactly this reason, and every AAA engine has
// the same layer under a different name -- Unreal's UPROPERTY, Unity's
// [SerializeField], Naughty Dog's DC schemas. What they buy is not elegance:
// it is that **adding a component is one table entry**. Without it, a new
// component means a serialiser, a deserialiser, an inspector drawer and a menu
// row, four places to forget, and the ones that get forgotten fail silently --
// the component saves but does not load, or loads but cannot be edited.
//
// Deliberately small. No inheritance, no attributes, no code generation: a
// component is a POD struct and a field is a (name, type, offset) triple, which
// is all a byte stream or an ImGui widget actually needs.
enum class FieldType : uint8_t {
    Bool,
    Int,
    Float,
    Vec3,
    Quat,
    Colour, // a vec3 the inspector shows as a colour swatch, not three sliders
    String,
};

struct Field {
    const char* name = nullptr;
    FieldType type = FieldType::Float;
    // Byte offset within the component struct (offsetof). uint16_t because a
    // component that needs more than 64 KiB of fields is not a component.
    uint16_t offset = 0;
    // Inspector range for Int/Float. max <= min means unbounded, which is also
    // the default, so a field only declares a range when it has one.
    float min = 0.0f;
    float max = 0.0f;
};

// A struct's fields, as a non-owning view over a static array.
struct FieldSpan {
    const Field* data = nullptr;
    int count = 0;
};

// A type's field list. Specialise this next to where the type is registered and
// every consumer -- serialiser, inspector, shader-block push -- works from it.
template <typename T> FieldSpan fieldsOf();

// A field's address inside a live component instance.
inline void* fieldPtr(void* component, const Field& f)
{
    return static_cast<std::byte*>(component) + f.offset;
}
inline const void* fieldPtr(const void* component, const Field& f)
{
    return static_cast<const std::byte*>(component) + f.offset;
}

// Declares a field of `Struct`. The macro exists so the name in the UI and the
// member it edits cannot drift apart -- they are the same token.
#define ENG_FIELD(Struct, member, type)                                        \
    ::eng::Field                                                          \
    {                                                                          \
        #member, type, uint16_t(offsetof(Struct, member)), 0.0f, 0.0f          \
    }
#define ENG_FIELD_RANGE(Struct, member, type, lo, hi)                          \
    ::eng::Field                                                          \
    {                                                                          \
        #member, type, uint16_t(offsetof(Struct, member)), lo, hi              \
    }

} // namespace eng
