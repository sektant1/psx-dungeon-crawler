#include <editor/content/SceneContract.h>


namespace game::content {
namespace {

// Whether an entity carries something to look through, and which shape it is.
// The order is the specificity order the runtime already uses: a ScreenCamera
// makes the scene a page whatever else is on the entity, and a rig component
// beats a plain Camera because it brings the tuning that decides how the scene
// plays.
bool hasView(const Entity& e)
{
    return e.camera.has_value() || e.firstPerson.has_value() ||
           e.thirdPerson.has_value() || e.screen.has_value();
}

SceneKind kindOf(const Entity& e)
{
    if (e.screen)
        return SceneKind::Screen;
    if (e.thirdPerson)
        return SceneKind::ThirdPerson;
    if (e.firstPerson)
        return SceneKind::FirstPerson;
    return SceneKind::Shot;
}

// A world scene is one the player walks around in. The ones that are not are a
// shot (which plays itself) and a screen (which is a flat page), and most of
// the roles below only make sense for a world.
//
// GameDriven counts: a dungeon level that authors no camera is still a level.
bool isWorld(SceneKind kind)
{
    return kind == SceneKind::FirstPerson || kind == SceneKind::ThirdPerson ||
           kind == SceneKind::GameDriven;
}

RoleStatus role(SceneRole which, bool applicable, Severity severity,
                QuickFix fix = QuickFix::None)
{
    RoleStatus status;
    status.role = which;
    status.applicable = applicable;
    status.severity = severity;
    status.fix = fix;
    return status;
}

} // namespace

const char* sceneKindName(SceneKind kind)
{
    switch (kind) {
    case SceneKind::Empty:       return "Empty";
    case SceneKind::GameDriven:  return "Level";
    case SceneKind::Shot:        return "Shot";
    case SceneKind::FirstPerson: return "First person";
    case SceneKind::ThirdPerson: return "Third person";
    case SceneKind::Screen:      return "2D screen";
    }
    return "Empty";
}

const char* sceneKindSummary(SceneKind kind)
{
    switch (kind) {
    case SceneKind::Empty:
        return "Nothing to look through and nobody to look. This scene will "
               "load, cook and play, and show nothing.";
    case SceneKind::GameDriven:
        return "A level the game supplies the camera for: it authors a player "
               "spawn and no view, so the player controller drives it. What "
               "nearly every dungeon level is. Author a camera component only "
               "to override that.";
    case SceneKind::Shot:
        return "Plays itself through its own camera. The player controller "
               "stands down and the mouse stays free, which is what makes it "
               "recordable with --record.";
    case SceneKind::FirstPerson:
        return "A level seen from the player's eyes.";
    case SceneKind::ThirdPerson:
        return "A level seen over the player's shoulder, with a lock-on.";
    case SceneKind::Screen:
        return "Not a world: a 2D page authored in pixels. A menu, a HUD plate "
               "or a dialogue screen.";
    }
    return "";
}

const char* sceneRoleName(SceneRole which)
{
    switch (which) {
    case SceneRole::View:        return "View";
    case SceneRole::Spawn:       return "Player spawn";
    case SceneRole::Listener:    return "Audio listener";
    case SceneRole::KeyLight:    return "Key light";
    case SceneRole::Environment: return "Environment";
    case SceneRole::Exit:        return "Exit";
    }
    return "?";
}

ContractReport sceneContract(const SceneDocument& document)
{
    ContractReport report;

    // Pass 1: find the views, because the kind decides which roles apply.
    // The highest-priority one wins, matching SceneSync's rule at runtime --
    // otherwise the panel would name one camera and the game would use another.
    const Entity* chosen = nullptr;
    int bestPriority = 0;
    // Active views only. `report.views` lists every view entity including the
    // parked ones -- the panel wants to show them -- but a parked camera is
    // explicitly "kept, not used", so counting it as filling the role reported
    // a scene with nothing but a parked camera as playable. It is not: the
    // runtime skips it exactly as this does.
    int activeViews = 0;
    for (const Entity& e : document.entities) {
        if (!hasView(e))
            continue;
        report.views.push_back(e.id);
        const bool active = !e.camera || e.camera->active;
        if (!active)
            continue;
        ++activeViews;
        const int priority = e.camera ? e.camera->priority : 0;
        if (!chosen || priority > bestPriority) {
            chosen = &e;
            bestPriority = priority;
        }
    }
    // Pass 2: count the fillers. Before the kind is settled, because whether
    // there is a player spawn is what decides Empty from GameDriven.
    int spawns = 0, listeners = 0, keyLights = 0, exits = 0;
    AuthorId spawnId, listenerId, keyLightId, exitId;
    for (const Entity& e : document.entities) {
        // A player spawn, or an active first-person rig -- which says the same
        // thing and more. `first_person` states how the player moves AND, by
        // its transform, where they are; eng::runtime::SceneRuntime::playerSpawn
        // reads exactly that and stands the player there.
        //
        // This matters beyond tidiness: PlayerSpawn is one of THIS game's
        // markers, so before this a scene made in a project -- which has no
        // game components at all -- could not fill the role however it was
        // authored, and every new project's first cook was a refusal.
        //
        // A parked rig (active = false) does not count, for the same reason a
        // parked camera does not: it is kept, not used.
        const bool firstPersonSpawn = e.firstPerson && e.firstPerson->active;
        if (e.playerSpawn || firstPersonSpawn) {
            ++spawns;
            spawnId = e.id;
        }
        if (e.audioListener) {
            ++listeners;
            listenerId = e.id;
        }
        if (e.light && e.light->type == LightAuthor::Type::Directional) {
            ++keyLights;
            keyLightId = e.id;
        }
        if (e.exitYawDegrees) {
            ++exits;
            exitId = e.id;
        }
    }

    report.kind = chosen          ? kindOf(*chosen)
                  : spawns > 0    ? SceneKind::GameDriven
                                  : SceneKind::Empty;
    const bool world = isWorld(report.kind);

    // --- View ------------------------------------------------------------
    // Filled by an authored view OR by a player spawn -- the game supplies the
    // camera for the latter, and a level that authors none is deferring to the
    // player controller rather than being broken. Only a scene with neither is
    // an Error, and that is the failure this file was written for.
    {
        RoleStatus status = role(SceneRole::View, true, Severity::Error,
                                 QuickFix::AddFirstPersonView);
        status.count = activeViews;
        if (chosen)
            status.filledBy = chosen->id;
        const int parked = int(report.views.size()) - activeViews;
        if (activeViews == 0 && report.kind == SceneKind::GameDriven) {
            // Satisfied, by the spawn. Counted as one so the role reads as
            // filled everywhere -- the panel, the validator and `playable` all
            // key off the count and must not each re-derive this rule.
            status.count = 1;
            status.filledBy = spawnId;
            status.detail = "The game's player camera (no view authored).";
            status.fix = QuickFix::None;
        } else if (activeViews == 0) {
            // Naming the parked ones matters: "no camera" on a scene that
            // visibly has a camera entity is the message that sends someone
            // looking for the wrong problem.
            status.detail =
                parked > 0
                    ? std::to_string(parked) +
                          " view(s), all parked (active = false). Nothing to "
                          "look through until one is enabled."
                    : "No camera, controller, screen or player spawn. Add one "
                      "to see anything.";
        } else if (activeViews > 1) {
            // Legal: a debug or death cam takes over by existing at a higher
            // priority, and neither camera knows about the other. Reported
            // because two views at the *same* priority is a coin toss.
            status.detail = std::to_string(activeViews) +
                            " active views; the highest priority wins.";
            status.fix = QuickFix::None;
        } else {
            status.detail = std::string(sceneKindName(report.kind)) + ".";
            status.fix = QuickFix::None;
        }
        report.roles.push_back(std::move(status));
    }

    // --- Player spawn ----------------------------------------------------
    // Only a world scene has a player to place. A shot plays itself and a
    // screen is a page, and demanding a spawn from either would train people to
    // ignore this panel.
    {
        RoleStatus status = role(SceneRole::Spawn, world, Severity::Error,
                                 QuickFix::AddPlayerSpawn);
        status.count = spawns;
        status.filledBy = spawns == 1 ? spawnId : AuthorId{};
        status.detail = spawns == 0   ? "The player has nowhere to start."
                        : spawns == 1 ? "One."
                                      : std::to_string(spawns) +
                                            " spawns; the first one found wins.";
        if (spawns > 0)
            status.fix = QuickFix::None;
        report.roles.push_back(std::move(status));
    }

    // --- Audio listener --------------------------------------------------
    // A warning, not an error: the game attaches a listener to the player
    // camera when a scene authors none, so silence is only *likely*, not
    // certain. Two is the interesting case and is what this catches.
    {
        RoleStatus status = role(SceneRole::Listener, report.kind != SceneKind::Empty,
                                 Severity::Warning, QuickFix::AddAudioListener);
        status.count = listeners;
        status.filledBy = listeners == 1 ? listenerId : AuthorId{};
        status.detail =
            listeners == 0   ? "None authored; the player camera hears."
            : listeners == 1 ? "One."
                             : std::to_string(listeners) +
                                   " listeners -- positional audio is undefined "
                                   "with more than one.";
        if (listeners == 1)
            status.fix = QuickFix::None;
        report.roles.push_back(std::move(status));
    }

    // --- Key light -------------------------------------------------------
    {
        RoleStatus status = role(SceneRole::KeyLight, world, Severity::Warning,
                                 QuickFix::AddKeyLight);
        status.count = keyLights;
        status.filledBy = keyLights == 1 ? keyLightId : AuthorId{};
        status.detail = keyLights == 0
                            ? "No directional light; only point lights will lit "
                              "this level."
                            : std::to_string(keyLights) + " directional.";
        if (keyLights > 0)
            status.fix = QuickFix::None;
        report.roles.push_back(std::move(status));
    }

    // --- Environment -----------------------------------------------------
    // Informational always: a scene with no palette gets the game's default,
    // which is a real answer and not a hole.
    {
        RoleStatus status = role(SceneRole::Environment, true, Severity::Info);
        status.count = document.palette.empty() ? 0 : 1;
        status.detail = document.palette.empty()
                            ? "No palette; the game's default grading applies."
                            : "Palette '" + document.palette + "'.";
        report.roles.push_back(std::move(status));
    }

    // --- Exit ------------------------------------------------------------
    // A dungeon rule rather than a scene rule, which is why it is Info here and
    // why `exit.missing` in the validator stays where it is: the validator
    // knows the level is a dungeon, this file only knows it is a world.
    {
        RoleStatus status = role(SceneRole::Exit, world, Severity::Info);
        status.count = exits;
        status.filledBy = exits == 1 ? exitId : AuthorId{};
        status.detail = exits == 0 ? "None; the level does not end."
                                   : std::to_string(exits) + ".";
        report.roles.push_back(std::move(status));
    }

    report.playable = true;
    for (const RoleStatus& status : report.roles) {
        if (status.applicable && status.severity == Severity::Error &&
            status.count == 0)
            report.playable = false;
    }
    return report;
}

AuthorId setSceneView(SceneDocument& document, SceneKind kind)
{
    // Empty and GameDriven are not shapes to set: they are what a scene is when
    // nothing is authored. "Make this game-driven" is deleting the camera, and
    // deleting an entity is the caller's decision, not this function's.
    if (kind == SceneKind::Empty || kind == SceneKind::GameDriven)
        return {};

    // The entity that already is the view, if there is one. Reused rather than
    // replaced: the camera is *where it is*, and re-placing it is the part
    // nobody wants to redo when they are only changing the shape.
    Entity* target = nullptr;
    for (Entity& e : document.entities) {
        if (!hasView(e))
            continue;
        if (!target || (e.camera && target->camera &&
                        e.camera->priority > target->camera->priority))
            target = &e;
    }

    if (!target) {
        Entity created;
        created.id = document.allocateId("camera");
        created.name = "Camera";
        // Eye height at the origin rather than the origin itself: a camera on
        // the floor is the first thing anyone would have had to fix.
        created.transform.position = {0.0f, 1.7f, 0.0f};
        document.add(created);
        target = document.find(created.id);
        if (!target)
            return {};
    }

    // Exactly one shape survives. Carrying two would leave the runtime to pick,
    // and which one it picks is the thing this function exists to make explicit.
    target->firstPerson.reset();
    target->thirdPerson.reset();
    target->screen.reset();

    switch (kind) {
    case SceneKind::FirstPerson:
        target->firstPerson = FirstPersonAuthor{};
        break;
    case SceneKind::ThirdPerson:
        target->thirdPerson = ThirdPersonAuthor{};
        break;
    case SceneKind::Screen:
        target->screen = ScreenAuthor{};
        break;
    case SceneKind::Shot:
        // A plain Camera and nothing else. The lens has to exist or the entity
        // stops being a view at all.
        if (!target->camera)
            target->camera = CameraAuthor{};
        break;
    case SceneKind::Empty:
    case SceneKind::GameDriven:
        // Neither is a shape an entity can carry: they are what a scene IS when
        // no view is authored. Refused at the top of the function, and listed
        // here so adding a kind cannot silently fall through this switch.
        break;
    }
    // Every shape wants a lens: the rigs read fov and the clip planes from it,
    // and a scene whose camera had none rendered with whatever the last one set.
    if (!target->camera)
        target->camera = CameraAuthor{};

    document.touch();
    return target->id;
}

} // namespace game::content
