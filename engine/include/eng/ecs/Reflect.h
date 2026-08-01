#pragma once
#include <eng/Reflect.h>

namespace eng::ecs {

// Reflection moved down to eng/Reflect.h: the renderer pushes a component's
// fields as shader uniforms, and eng/ecs is a layer above it. These aliases
// keep `eng::ecs::Field` reading correctly at the component call sites, where
// the fields being described really are a component's.
using Field = eng::Field;
using FieldSpan = eng::FieldSpan;
using FieldType = eng::FieldType;
using eng::fieldPtr;
using eng::fieldsOf;

} // namespace eng::ecs
