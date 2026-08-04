// The socket maths and the hands definition that names them.
//
// Renderer-free by construction: socketLocalMatrix/socketTransform are free
// functions over a joint matrix, which is the whole reason attaching a weapon
// to a hand is testable without a GPU, a skeleton or a cooked asset.

#include "HandsDefinition.h"
#include "ViewmodelSocket.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ViewmodelSocketTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(float a, float b, float epsilon = 0.0005f)
{
    return std::abs(a - b) <= epsilon;
}

bool nearVec(glm::vec3 a, glm::vec3 b, float epsilon = 0.0005f)
{
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) &&
           near(a.z, b.z, epsilon);
}

void identitySocketIsTheJoint()
{
    // The muzzle's fallback path relies on this exactly: a weapon that names a
    // raw joint gets an identity socket, and must land where the old bespoke
    // arithmetic put it. If this drifts, every shipped weapon's muzzle moves.
    const glm::mat4 joint =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 1.4f, -0.2f)) *
        glm::mat4_cast(glm::quat(glm::radians(glm::vec3(20.0f, -35.0f, 8.0f))));
    game::ViewmodelSocketDef socket;
    const glm::mat4 result = game::socketLocalMatrix(joint, socket);
    for (int column = 0; column < 4; ++column)
        require(nearVec(glm::vec3(result[column]), glm::vec3(joint[column])),
                "an identity socket must be the joint itself");
}

void offsetIsAppliedInJointSpace()
{
    // A socket offset rides the joint's rotation: this is what makes "10 cm
    // along the bone" mean the same thing whatever the hand is doing.
    const glm::mat4 joint =
        glm::mat4_cast(glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f))));
    game::ViewmodelSocketDef socket;
    socket.offset = glm::vec3(0.0f, 0.0f, -1.0f);
    const game::SocketTransform t = game::socketTransform(joint, socket);
    // -z rotated 90 degrees about +y is -x.
    require(nearVec(t.position, glm::vec3(-1.0f, 0.0f, 0.0f)),
            "the offset must be rotated into joint space");
}

void transformDecomposesScaleAndRotation()
{
    const glm::mat4 joint =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    game::ViewmodelSocketDef socket;
    socket.offset = glm::vec3(0.1f, 0.0f, 0.0f);
    socket.rotationDegrees = glm::vec3(0.0f, 45.0f, 0.0f);
    socket.scale = 0.5f;

    const game::SocketTransform t = game::socketTransform(joint, socket);
    require(nearVec(t.position, glm::vec3(1.1f, 2.0f, 3.0f)),
            "translation must survive decomposition");
    require(nearVec(t.scale, glm::vec3(0.5f)),
            "socket scale must come back out as scale, not bake into rotation");
    const glm::vec3 forward = t.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    require(nearVec(forward, glm::vec3(-std::sqrt(0.5f), 0.0f,
                                       -std::sqrt(0.5f)), 0.002f),
            "a 45 degree yaw must survive decomposition");
}

void invalidSocketsAreRejected()
{
    game::ViewmodelSocketDef socket;
    socket.name = "right_hand";
    socket.joint = "hand.R";
    require(game::validViewmodelSocket(socket), "a plain socket is valid");

    game::ViewmodelSocketDef unnamed = socket;
    unnamed.name.clear();
    require(!game::validViewmodelSocket(unnamed),
            "a socket with no name cannot be referenced, so it is invalid");

    game::ViewmodelSocketDef jointless = socket;
    jointless.joint.clear();
    require(!game::validViewmodelSocket(jointless),
            "a socket with no joint has nothing to ride");

    game::ViewmodelSocketDef zeroScale = socket;
    zeroScale.scale = 0.0f;
    require(!game::validViewmodelSocket(zeroScale),
            "a zero scale collapses whatever hangs on it");

    game::ViewmodelSocketDef notFinite = socket;
    notFinite.offset.x = std::nanf("");
    require(!game::validViewmodelSocket(notFinite),
            "a non-finite offset must not reach the renderer");
}

void defaultsCoverTheShippedWeapons()
{
    const game::HandsDefinition hands = game::defaultHandsDefinition();
    require(game::validHandsDefinition(hands), "the shipped default is valid");

    const auto has = [&](const char* name) {
        for (const game::ViewmodelSocketDef& socket : hands.sockets)
            if (socket.name == name)
                return true;
        return false;
    };
    // The two the shipped loadout names. A rename here silently drops a weapon
    // out of the player's hand, so it is pinned.
    require(has("right_hand"), "right_hand is what both weapons hang on");
    require(has("right_index_tip"), "the talon fires from the index fingertip");
    require(has("right_middle_tip"), "the arbalest fires from the middle one");

    // Zero offsets: the weapon's own hands_muzzle_offset is the nudge, and a
    // socket that also carried one would double it.
    for (const game::ViewmodelSocketDef& socket : hands.sockets)
        require(nearVec(socket.offset, glm::vec3(0.0f)),
                "the default sockets must not carry an offset of their own");
}

void duplicateSocketNamesAreRejected()
{
    game::HandsDefinition hands;
    hands.sockets = {{"grip", "hand.R", {}, {}, 1.0f},
                     {"grip", "hand.L", {}, {}, 1.0f}};
    require(!game::validHandsDefinition(hands),
            "two sockets of one name make `socket = \"grip\"` ambiguous");
}

void parsesAnAuthoredRig()
{
    game::HandsDefinition hands = game::defaultHandsDefinition();
    const char* source = R"(
[hands]
model = "meshes/viewmodels/other_arms.glb"
idle_animation = "rest"

[[hands.socket]]
name = "grip"
joint = "hand.L"
offset = [0.0, 0.05, 0.0]
rotation = [0.0, 90.0, 0.0]
scale = 2.0
)";
    require(game::parseHandsDefinition(source, hands), "the table parses");
    require(hands.model == "meshes/viewmodels/other_arms.glb",
            "an authored model replaces the default");
    require(hands.idleAnimation == "rest", "an authored idle clip is taken");
    require(hands.skeleton == game::defaultHandsDefinition().skeleton,
            "a key the file omits keeps the default");
    // Replaced, not merged: half a vocabulary is the confusing case, where a
    // weapon names a socket the file appears not to define and finds one anyway.
    require(hands.sockets.size() == 1,
            "an authored socket list replaces the defaults outright");
    require(hands.sockets[0].name == "grip" &&
                hands.sockets[0].joint == "hand.L" &&
                near(hands.sockets[0].scale, 2.0f),
            "the socket's fields survive the round trip");
}

void aRejectedRigLeavesTheCallerUntouched()
{
    game::HandsDefinition hands = game::defaultHandsDefinition();
    const std::size_t before = hands.sockets.size();
    // A socket with no joint: invalid, so the whole table is refused rather
    // than applied in half. A partly-applied rig is worse than the shipped one.
    require(!game::parseHandsDefinition(R"(
[hands]
[[hands.socket]]
name = "grip"
)",
                                        hands),
            "a socket with no joint is rejected");
    require(hands.sockets.size() == before,
            "a rejected table must not have been half-applied");

    require(!game::parseHandsDefinition("[hands] this is not toml", hands),
            "a malformed document is rejected");
    require(hands.sockets.size() == before, "and still leaves the caller alone");
}

void noSectionIsNotAnError()
{
    game::HandsDefinition hands = game::defaultHandsDefinition();
    require(game::parseHandsDefinition("[something_else]\nkey = 1\n", hands),
            "a document without [hands] leaves the shipped rig in place");
    require(hands.sockets.size() == game::defaultHandsDefinition().sockets.size(),
            "and changes nothing");
}

} // namespace

int main()
{
    identitySocketIsTheJoint();
    offsetIsAppliedInJointSpace();
    transformDecomposesScaleAndRotation();
    invalidSocketsAreRejected();
    defaultsCoverTheShippedWeapons();
    duplicateSocketNamesAreRejected();
    parsesAnAuthoredRig();
    aRejectedRigLeavesTheCallerUntouched();
    noSectionIsNotAnError();
    std::cout << "ViewmodelSocketTests: ok\n";
    return 0;
}
