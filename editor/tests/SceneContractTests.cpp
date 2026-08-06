// What a scene needs to work, and what kind of scene it therefore is.
//
// The claims under test are the ones the editor's Scene panel, the validator
// and the cooker all rely on: the kind is derived from the view component, a
// role that does not apply to this kind is not reported, and swapping the view
// keeps the camera where the author put it.

#include <editor/content/SceneContract.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SceneContractTests: " << message << '\n';
        std::exit(1);
    }
}

static const RoleStatus& roleOf(const ContractReport& report, SceneRole which)
{
    const auto it = std::find_if(
        report.roles.begin(), report.roles.end(),
        [&](const RoleStatus& r) { return r.role == which; });
    require(it != report.roles.end(), "every role is reported");
    return *it;
}

static Entity& add(SceneDocument& document, const char* id)
{
    Entity entity;
    entity.id = id;
    return document.add(std::move(entity));
}

int main()
{
    // --- an empty document is Empty, and that is an Error ------------------
    {
        SceneDocument document;
        const ContractReport report = sceneContract(document);
        require(report.kind == SceneKind::Empty, "no view and no spawn is Empty");
        require(!report.playable, "and a scene nobody can look through is not playable");
        const RoleStatus& view = roleOf(report, SceneRole::View);
        require(view.count == 0, "no views counted");
        require(view.severity == Severity::Error, "the View role is the one Error");
        require(view.fix != QuickFix::None, "and it comes with a fix");
    }

    // --- a spawn with no camera is a LEVEL, not a fault --------------------
    //
    // The rule this pins down, and the one the first version of this file got
    // wrong: a World never touches the renderer's camera when a scene carries
    // none (docs/ecs.md), so a dungeon level that authors no camera is
    // deferring to the player controller. Nearly every level in the game is
    // this shape, and flagging them all would have made the panel noise.
    {
        SceneDocument document;
        Entity& spawn = add(document, "player_spawn");
        spawn.playerSpawn = true;

        const ContractReport report = sceneContract(document);
        require(report.kind == SceneKind::GameDriven,
                "a spawn with no view is a game-driven level");
        require(report.views.empty(), "and it really has no view component");
        const RoleStatus& view = roleOf(report, SceneRole::View);
        require(view.count == 1, "the View role is filled -- by the game");
        require(view.fix == QuickFix::None, "so there is nothing to fix");
        require(view.filledBy == "player_spawn", "and the panel can point at it");
        require(report.playable, "the level is playable as authored");
    }

    // --- the kind is derived from the view component -----------------------
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.camera = CameraAuthor{};
        require(sceneContract(document).kind == SceneKind::Shot,
                "a plain Camera is a shot");

        camera.firstPerson = FirstPersonAuthor{};
        require(sceneContract(document).kind == SceneKind::FirstPerson,
                "a controller makes it a first-person level");

        camera.firstPerson.reset();
        camera.thirdPerson = ThirdPersonAuthor{};
        require(sceneContract(document).kind == SceneKind::ThirdPerson,
                "a third-person camera makes it over-the-shoulder");

        camera.screen = ScreenAuthor{};
        require(sceneContract(document).kind == SceneKind::Screen,
                "a ScreenCamera wins outright -- it is not a world at all");
    }

    // --- roles that do not apply are not reported --------------------------
    //
    // The failure this prevents: a 2D menu reporting "no player spawn" and "no
    // key light" trains people to ignore the panel, which is exactly what a
    // checklist must not do.
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.camera = CameraAuthor{};
        camera.screen = ScreenAuthor{};

        const ContractReport report = sceneContract(document);
        require(report.kind == SceneKind::Screen, "a screen");
        require(!roleOf(report, SceneRole::Spawn).applicable,
                "a page has no player to place");
        require(!roleOf(report, SceneRole::KeyLight).applicable,
                "and nothing to light");
        require(report.playable,
                "a screen with a camera is playable -- it needs nothing else");
    }

    // A world scene DOES need a spawn, and says so.
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.camera = CameraAuthor{};
        camera.firstPerson = FirstPersonAuthor{};

        ContractReport report = sceneContract(document);
        require(roleOf(report, SceneRole::Spawn).applicable, "a world has a player");
        require(roleOf(report, SceneRole::Spawn).count == 0, "who has nowhere to start");
        require(!report.playable, "so it is not playable yet");

        Entity& spawn = add(document, "spawn");
        spawn.playerSpawn = true;
        report = sceneContract(document);
        require(report.playable, "and playable once it has one");
    }

    // --- over-filling: two views is legal, two listeners is not ------------
    {
        SceneDocument document;
        Entity& a = add(document, "cam_a");
        a.camera = CameraAuthor{};
        Entity& b = add(document, "cam_b");
        CameraAuthor high;
        high.priority = 10;
        b.camera = high;

        const ContractReport report = sceneContract(document);
        require(report.views.size() == 2, "both views are found");
        require(roleOf(report, SceneRole::View).filledBy == "cam_b",
                "the highest active priority is the one the game looks through, "
                "and the panel must name the same one");
        require(roleOf(report, SceneRole::View).fix == QuickFix::None,
                "two cameras is a debug cam taking over, not a fault");
    }
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.camera = CameraAuthor{};
        add(document, "ear_a").audioListener = AudioListenerAuthor{};
        add(document, "ear_b").audioListener = AudioListenerAuthor{};

        const RoleStatus& ears =
            roleOf(sceneContract(document), SceneRole::Listener);
        require(ears.count == 2, "both listeners counted");
        require(ears.fix != QuickFix::None,
                "two listeners is reported -- positional audio is undefined, "
                "and nothing else in the editor would catch it");
    }

    // An inactive camera does not decide the kind: it is a parked alternate
    // framing, kept but not used.
    {
        SceneDocument document;
        Entity& parked = add(document, "parked");
        CameraAuthor off;
        off.active = false;
        parked.camera = off;
        parked.thirdPerson = ThirdPersonAuthor{};

        const ContractReport report = sceneContract(document);
        require(report.kind == SceneKind::Empty,
                "a parked camera is not a view the scene plays through");
        // The bug this pins: the role counted every view entity including the
        // parked ones, so a scene whose only camera was disabled reported the
        // View role as filled and `playable` as true, while showing nothing.
        require(roleOf(report, SceneRole::View).count == 0,
                "a parked view fills nothing");
        require(!report.playable, "and the scene is not playable");
        require(report.views.size() == 1,
                "but it is still listed -- the panel has to say the camera is "
                "there and parked, not that there is no camera");
    }

    // --- setSceneView reuses the camera entity -----------------------------
    //
    // The point of the operation: the camera is *where it is*, and changing the
    // shape must not cost the placement.
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.name = "Hero Cam";
        camera.camera = CameraAuthor{};
        camera.firstPerson = FirstPersonAuthor{};
        camera.transform.position = {3.0f, 1.7f, -8.0f};

        const AuthorId id = setSceneView(document, SceneKind::ThirdPerson);
        require(id == "cam", "the same entity carries the new shape");

        const Entity* after = document.find("cam");
        require(after != nullptr, "and still exists");
        require(after->name == "Hero Cam", "keeping its name");
        require(after->transform.position.x == 3.0f &&
                    after->transform.position.z == -8.0f,
                "and its placement");
        require(after->thirdPerson.has_value(), "with the new shape");
        require(!after->firstPerson.has_value(),
                "and only the new shape -- carrying two would leave the runtime "
                "to pick, which is what this function exists to prevent");
        require(sceneContract(document).kind == SceneKind::ThirdPerson, "swapped");
    }

    // With no camera at all it creates one, at eye height rather than on the
    // floor.
    {
        SceneDocument document;
        const AuthorId id = setSceneView(document, SceneKind::FirstPerson);
        require(!id.empty(), "a camera is created");
        const Entity* camera = document.find(id);
        require(camera != nullptr, "and findable");
        require(camera->camera.has_value(),
                "every shape gets a lens -- the rigs read fov and the clip "
                "planes from it");
        require(camera->transform.position.y > 1.0f,
                "at eye height, not on the floor");
        require(sceneContract(document).kind == SceneKind::FirstPerson, "as asked");
    }

    // Making a scene 2D is the same one call, which is what makes "everything
    // is a scene" true in practice rather than only in principle.
    {
        SceneDocument document;
        Entity& camera = add(document, "cam");
        camera.camera = CameraAuthor{};
        camera.thirdPerson = ThirdPersonAuthor{};

        setSceneView(document, SceneKind::Screen);
        const ContractReport report = sceneContract(document);
        require(report.kind == SceneKind::Screen, "now a page");
        require(!document.find("cam")->thirdPerson.has_value(),
                "and no longer a world");
    }

    // --- the Warning holes are information, not faults ---------------------
    //
    // A minimal level -- a spawn and nothing else -- has no authored audio
    // listener and no directional key light, and both are correct: the player's
    // camera hears, and a torch-lit dungeon is point lights. The contract must
    // still SAY so (the panel shows it) while `playable` stays true, because a
    // rule that called every correct level in the repo faulty is how a panel
    // gets ignored. SceneValidateTests covers the other half -- that neither
    // reaches the Problems list.
    {
        SceneDocument document;
        Entity& spawn = add(document, "spawn");
        spawn.playerSpawn = true;

        const ContractReport report = sceneContract(document);
        require(report.playable, "a minimal level is playable");
        const RoleStatus& ears = roleOf(report, SceneRole::Listener);
        const RoleStatus& key = roleOf(report, SceneRole::KeyLight);
        require(ears.count == 0 && ears.severity == Severity::Warning,
                "the missing listener is reported as a warning, not an error");
        require(key.count == 0 && key.severity == Severity::Warning,
                "and so is the missing key light");
        require(!ears.detail.empty() && !key.detail.empty(),
                "both still explain themselves -- the panel shows the reason");
    }

    std::cout << "SceneContractTests OK\n";
    return 0;
}
