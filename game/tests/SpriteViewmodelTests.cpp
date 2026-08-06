#include "SpriteViewmodel.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SpriteViewmodelTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

game::ViewmodelSpriteLayer layer(const char* id, int gridX, int gridY)
{
    game::ViewmodelSpriteLayer value;
    value.id = id;
    value.material = "Game/Viewmodel/Sprite/Talon";
    value.grid = {gridX, gridY};
    return value;
}

} // namespace

// The renderer-free half of the sprite viewmodel: which layers are accepted,
// and what a frame index is allowed to be. The composition itself needs a GPU;
// this is the part that decides whether a bad TOML row reaches one.
int main()
{
    using namespace game;

    require(validViewmodelSpriteLayer(layer("weapon", 2, 2)),
            "a plain single-frame layer was rejected");

    // A layer must name something to draw and something to call it. Both have
    // been typos in practice, and an unnamed layer cannot be reported.
    ViewmodelSpriteLayer nameless = layer("weapon", 1, 1);
    nameless.id.clear();
    require(!validViewmodelSpriteLayer(nameless), "an unnamed layer was accepted");
    ViewmodelSpriteLayer bare = layer("weapon", 1, 1);
    bare.material.clear();
    require(!validViewmodelSpriteLayer(bare),
            "a layer with no material was accepted");

    // Frame indices must be inside the atlas. A cell past the end samples a
    // neighbour, which reads as a sprite flickering into someone else's art
    // rather than as a number that is wrong -- so it is rejected, not clamped.
    ViewmodelSpriteLayer past = layer("weapon", 2, 2);
    past.idleFrame = 4; // a 2x2 atlas has cells 0..3
    require(!validViewmodelSpriteLayer(past),
            "an idle frame past the end of the atlas was accepted");

    ViewmodelSpriteLayer run = layer("weapon", 2, 2);
    run.fireFrame = 2;
    run.fireFrameCount = 3; // 2,3,4 -- one past the end
    require(!validViewmodelSpriteLayer(run),
            "a firing run that overruns the atlas was accepted");
    run.fireFrameCount = 2; // 2,3 -- fits exactly
    require(validViewmodelSpriteLayer(run),
            "a firing run that exactly fills the atlas was rejected");

    // A negative index is the same failure from the other side.
    ViewmodelSpriteLayer negative = layer("weapon", 2, 2);
    negative.fireFrame = -1;
    require(!validViewmodelSpriteLayer(negative),
            "a negative fire frame was accepted");

    // Geometry has to be real: a zero-size quad draws nothing and a
    // zero-distance one sits exactly on the eye.
    ViewmodelSpriteLayer flat = layer("weapon", 1, 1);
    flat.size = {0.0f, 1.0f};
    require(!validViewmodelSpriteLayer(flat), "a zero-width layer was accepted");
    ViewmodelSpriteLayer onEye = layer("weapon", 1, 1);
    onEye.distance = 0.0f;
    require(!validViewmodelSpriteLayer(onEye),
            "a layer sitting on the eye was accepted");

    // A grid must have cells at all -- 0 columns divides by zero in the frame
    // maths downstream.
    require(!validViewmodelSpriteLayer(layer("weapon", 0, 1)),
            "a layer with no atlas columns was accepted");

    // fireFrameCount == 1 means "does not animate on fire", which is the right
    // authoring for a static hand layer and must stay legal.
    ViewmodelSpriteLayer still = layer("left_hand", 1, 1);
    still.fireFrameCount = 1;
    require(validViewmodelSpriteLayer(still),
            "a non-animating layer was rejected");

    std::cout << "SpriteViewmodelTests OK\n";
    return EXIT_SUCCESS;
}
