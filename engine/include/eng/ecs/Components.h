#pragma once

// Every engine component, one per file under components/.
//
// Include this when you want the lot (a system that touches several, a
// serialiser); include the individual header when you want one -- a header that
// only needs Transform should not pull in Jolt's layer enum through Collider.
//
// ## Adding a component
//
// 1. Write `components/<Name>.h`: a POD struct, and a comment saying what it
//    means and who writes it. That comment is the design; the struct is the
//    consequence.
// 2. Add it to the list below.
// 3. If it should save, load and be editable, give it a `fieldsOf<T>()`
//    specialisation and one `reflectedComponent<T>(...)` line in
//    ComponentRegistry.cpp. That is the whole registration -- the .map payload,
//    the inspector rows and the add-component menu all come from the field
//    table (see docs/ecs.md).
// 4. If something has to *happen* when it is present, that belongs in a
//    reconciler (SceneSync, PhysicsSync), not in the component.
//
// Nothing else. No base class to derive from, no factory to register with, no
// virtual to override -- which is the property that makes a component cheap
// enough to add for one entity's sake.

// --- identity and hierarchy ---
#include <eng/ecs/components/Children.h>
#include <eng/ecs/components/Dirty.h>
#include <eng/ecs/components/EntityGroup.h>
#include <eng/ecs/components/Lifetime.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Orbit.h>
#include <eng/ecs/components/Parent.h>
#include <eng/ecs/components/Spin.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>

// --- audio ---
#include <eng/ecs/components/AudioEmitter.h>
#include <eng/ecs/components/AudioListener.h>

// --- rendering ---
#include <eng/ecs/components/Camera.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/LightAnimation.h>
#include <eng/ecs/components/LightColour.h>
#include <eng/ecs/components/LightRef.h>
#include <eng/ecs/components/MaterialApplied.h>
#include <eng/ecs/components/MaterialOverride.h>
#include <eng/ecs/components/MeshRenderer.h>
#include <eng/ecs/components/MeshSource.h>
#include <eng/ecs/components/NodeRef.h>
#include <eng/ecs/components/PrimitiveMesh.h>
#include <eng/ecs/components/RenderNode.h>
#include <eng/ecs/components/ShaderParams.h>
#include <eng/ecs/components/ShaderParamsApplied.h>
#include <eng/ecs/components/Visibility.h>

// --- vfx ---
#include <eng/ecs/components/ParticleEmitter.h>
#include <eng/ecs/components/PortalParams.h>
#include <eng/ecs/components/ParticlesRef.h>

// --- physics ---
#include <eng/ecs/components/BodyRef.h>
#include <eng/ecs/components/Collider.h>
#include <eng/ecs/components/KinematicControl.h>
#include <eng/ecs/components/RigidBody.h>
