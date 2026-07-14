#pragma once
#include <Ogre.h>

#include <cmath>

// Ports of the three GDScript animation behaviours. All take an absolute
// animation time so pause/restart (scene_controls.gd) is just freezing /
// zeroing the clock.

// world/orbit_camera.gd: rotation.y = default + time * ROTATION_SPEED (1.0)
struct OrbitCamera {
    Ogre::SceneNode* node = nullptr;
    float baseYaw = 0.0f; // from the OrbitPoint Transform3D in world.tscn

    void update(float time) const
    {
        node->setOrientation(
            Ogre::Quaternion(Ogre::Radian(baseYaw + time), Ogre::Vector3::UNIT_Y));
    }
};

// world/spatial_sin_pan.gd: transform = default * offset where
//   offset = T(0, sin(t)*dir, 0) * Basis.from_euler(t,t,t)  [YXZ order]
// Implemented as a child "offset" node under a node holding the default
// transform -- parent*child composition == Godot's default * offset.
struct SinPan {
    Ogre::SceneNode* offsetNode = nullptr;
    bool reverseDirection = false;

    static Ogre::Quaternion eulerYXZ(float a)
    {
        const Ogre::Radian r(a);
        return Ogre::Quaternion(r, Ogre::Vector3::UNIT_Y) *
               Ogre::Quaternion(r, Ogre::Vector3::UNIT_X) *
               Ogre::Quaternion(r, Ogre::Vector3::UNIT_Z);
    }

    void update(float time) const
    {
        const float dir = reverseDirection ? -1.0f : 1.0f;
        offsetNode->setPosition(0.0f, std::sin(time) * dir, 0.0f);
        offsetNode->setOrientation(eulerYXZ(time));
    }
};

// world/shadow/shadow.gd: scale = 0.775 + sin(t) * 0.125 * (reverse ? 1 : -1)
struct ShadowScale {
    Ogre::SceneNode* node = nullptr;
    bool reverseDirection = false;

    void update(float time) const
    {
        const float dir = reverseDirection ? 1.0f : -1.0f;
        const float s = 0.775f + std::sin(time) * 0.125f * dir;
        node->setScale(s, s, s);
    }
};
