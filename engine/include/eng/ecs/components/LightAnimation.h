#pragma once

namespace eng::ecs {

// How a light modulates over time. `lightAnimationSystem` reads this plus the
// LightRef's authored colour and writes the result into LightColour, which
// SceneSync already pushes every frame.
//
// LightColour's own comment says torch flicker is "gameplay writing a
// component". This is the component that means an *author* can write it: a
// torch in a .map flickers because the entity carries one of these, not because
// a C++ system knows which lights in this level are torches.
//
// Deterministic by construction: the modulation is a function of accumulated
// time and `phase`, with no global RNG, so two runs of the same level light it
// identically -- which is what makes a capture comparable frame for frame.
struct LightAnimation {
    enum Mode : int {
        Steady = 0,  // no modulation; useful to disable one without removing it
        Flicker = 1, // value-noise guttering, for fire
        Pulse = 2,   // smooth sine, for magic
    };

    // Held as int, not the enum: the reflection layer's field types are the ones
    // a byte stream and an ImGui widget both understand, and an enum that
    // serialises as an int cannot acquire a value the reader has never heard of.
    int mode = Flicker;
    float speed = 7.0f;  // modulations per second
    float amount = 0.3f; // depth, 0 = none, 1 = down to black at the trough
    // Per-instance offset. Two torches in one room with the same numbers and no
    // phase blink in lockstep, which reads as a light switch rather than fire.
    float phase = 0.0f;

    // Accumulated seconds. Runtime state, deliberately not reflected: it is not
    // authored, and a saved one would restart the flame mid-gutter.
    float time = 0.0f;
};

} // namespace eng::ecs
