#pragma once

#include <eng/AudioTypes.h>

#include <glm/vec3.hpp>

#include <string>

namespace eng::ecs {

// A sound source attached to an entity transform. AudioSync owns the live
// SoundInstance; this component contains only authored, serialisable state.
// `source` is a logical asset path such as "audio/ambience/crypt.ogg".
struct AudioEmitter {
    std::string source;
    glm::vec3 offset{0.0f};
    int bus = static_cast<int>(AudioBus::Sfx);
    float gainDb = 0.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 45.0f;
    float rolloff = 1.0f;
    float dopplerFactor = 1.0f;
    int priority = static_cast<int>(AudioPriority::Normal);
    bool loop = false;
    bool streaming = false;
    bool spatialized = true;
    bool playing = true;
    bool stealable = true;
};

} // namespace eng::ecs
