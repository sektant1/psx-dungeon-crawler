#pragma once
// Severity and QuickFix: a contract role that is unfilled becomes an issue like
// any other, so the editor's existing "fix it" affordance carries these too.
// The include points this way and never back -- SceneValidate.cpp reads the
// contract, SceneContract.h never reads the validator.
#include <editor/content/SceneValidate.h>

#include <string>
#include <vector>

namespace game::content {

// What a scene has to carry to work, and what kind of scene it therefore is.
//
// A scene is not "a list of entities". It is a list of entities that fills a
// set of ROLES, and until this file there was nowhere that said so: `validate()`
// has thirty-nine issue codes and every one of them is about an entity being
// wrong. There was no code for a scene with no camera and no player spawn --
// which loads, cooks, plays, and shows nothing.
//
// Three consumers read this one table, so they cannot disagree about what a
// scene needs: the validator (as issues with quick fixes), the editor's Scene
// panel (as a checklist a person can read), and the cooker (which refuses a
// scene that cannot be looked at).
//
// The scene's KIND is derived, never authored. Which view component is present
// already decides how the scene plays -- MapRuntime and MapPlay read exactly
// that today, in two `if`s in two files. This names the rule instead of leaving
// it implicit.

enum class SceneKind {
    // Nothing to look through and nobody to look: no view component and no
    // player spawn. The scene loads and shows nothing, which is the failure
    // this file exists to make loud.
    Empty,
    // No authored view, but a player spawn -- so the GAME supplies the camera.
    // This is what nearly every dungeon level is, and getting it wrong is what
    // the first version of this file did: `World` never touches the renderer's
    // camera when a scene carries none (docs/ecs.md), so a level that authors
    // no camera is not broken, it is deferring to the player controller.
    //
    // A distinct kind rather than "Empty with a spawn" because the two need
    // opposite reactions: one is a bug and the other is the common case.
    GameDriven,
    // A plain Camera: the scene plays itself as a shot. The player controller
    // stands down and the mouse stays free -- what makes a scene recordable.
    Shot,
    FirstPerson,
    ThirdPerson,
    // A ScreenCamera: not a world at all, a 2D page. Most roles below do not
    // apply to one, which is why the kind has to be known before they are
    // judged.
    Screen,
};

const char* sceneKindName(SceneKind kind);

// A short sentence saying what this kind of scene *is*, for the panel. The
// answer to "I don't know what this scene does".
const char* sceneKindSummary(SceneKind kind);

enum class SceneRole {
    View,        // something to look through -- the one universally required role
    Spawn,       // where the player starts
    Listener,    // where audio is heard from
    KeyLight,    // something to see by
    Environment, // the palette and fog this level is graded with
    Exit,        // where the level ends
};

const char* sceneRoleName(SceneRole role);

struct RoleStatus {
    SceneRole role = SceneRole::View;
    // How many entities fill it. The distinction that matters is 0 / 1 / many:
    // two audio listeners is as broken as none, and in a way nothing else in
    // the editor would report.
    int count = 0;
    // Whether this role applies to this kind of scene at all. A 2D screen needs
    // no key light and no player spawn, and reporting them as missing would
    // train people to ignore the panel.
    bool applicable = true;
    // What to do about it being unfilled -- Error blocks the cook, Warning does
    // not, Info is a note.
    Severity severity = Severity::Warning;
    // The entity filling it, when exactly one does. Lets the panel select it.
    AuthorId filledBy;
    // Human sentence: what fills this, or what is wrong.
    std::string detail;
    QuickFix fix = QuickFix::None;
};

struct ContractReport {
    SceneKind kind = SceneKind::Empty;
    std::vector<RoleStatus> roles;
    // True when every applicable required role is filled: the scene will show
    // something when it is played.
    bool playable = false;
    // The entities carrying a view component. More than one is legal only when
    // their Camera priorities differ, which is how a debug cam takes over --
    // so this is reported, not refused.
    std::vector<AuthorId> views;
};

// Evaluates the contract. Pure: no kit, no asset root, no I/O, so the editor
// can call it every frame and a test can call it in microseconds.
ContractReport sceneContract(const SceneDocument& document);

// The two operations the contract makes legal, and which were manual component
// surgery before: give a scene a view, or swap the one it has.
//
// Swapping preserves the entity, its transform and its name -- the camera is
// *where it is* and that is the part nobody wants to redo -- and replaces only
// the component that decides the shape. A scene with no view at all gets a new
// entity at the origin.
//
// Returns the entity that ends up carrying the view, or an empty id on failure.
AuthorId setSceneView(SceneDocument& document, SceneKind kind);

} // namespace game::content
