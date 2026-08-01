#pragma once
#include <eng/Reflect.h>

namespace eng {

// A block of shader uniforms, described by its own field table.
//
// This is what makes "every shader can have a component" one mechanism rather
// than one function per shader. A component whose **field names are the uniform
// names** can be pushed wholesale: the reflection layer already knows each
// field's name, type and offset (eng::Field), which is exactly the triple
// `setNamedConstant` needs.
//
//   struct PortalParams {              // fields named after the uniforms
//       float portalFlowSpeed = 0.35f;
//       float portalTwist     = 0.18f;
//       ...
//   };
//   template <> FieldSpan fieldsOf<PortalParams>() { ... }   // once
//
// and the renderer can drive it without ever having heard of portals. Adding a
// shader component is then a struct, a field table and one line in SceneSync --
// no renderer change, no new backend call, no per-shader plumbing.
//
// The rule the design rests on: **the field name IS the uniform name.** It is a
// strong constraint and it is the point. A mapping table would be a second
// place to be wrong, and being wrong in it is silent -- the uniform keeps its
// default and the slider does nothing. Named the same, a typo is a compile
// error in the field table or a miss the renderer can report.
struct ShaderBlock {
    FieldSpan fields;
    const void* instance = nullptr;

    bool valid() const { return fields.data && fields.count > 0 && instance; }
};

// Builds a block from any reflected component. `fieldsOf<T>()` must exist.
template <typename T> ShaderBlock shaderBlockOf(const T& value)
{
    return ShaderBlock{fieldsOf<T>(), &value};
}

} // namespace eng
