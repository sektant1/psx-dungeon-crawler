// The editor's marks for the entities that have no mesh.
//
// The property that matters: everything a level contains has to be visible and
// clickable in the viewport. A spawn, a trigger and a light were all invisible,
// and an invisible entity is one an author can only verify by cooking the map
// and running the game.

#include <editor/viewport/EntityGizmos.h>
#include <editor/scene/Picker.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::Entity;
using game::content::LightAuthor;
using game::content::SceneDocument;
using game::content::TriggerAuthor;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorEntityGizmoTests: " << message << '\n';
        std::exit(1);
    }
}

static bool has(const std::vector<GizmoMark>& marks, GizmoKind kind)
{
    for (const GizmoMark& mark : marks)
        if (mark.kind == kind)
            return true;
    return false;
}

static const GizmoMark* find(const std::vector<GizmoMark>& marks, GizmoKind kind)
{
    for (const GizmoMark& mark : marks)
        if (mark.kind == kind)
            return &mark;
    return nullptr;
}

int main()
{
    SceneDocument doc;

    Entity spawn;
    spawn.id = "player_spawn_0001";
    spawn.playerSpawn = true;
    spawn.transform.position = {2.0f, 0.0f, 3.0f};
    spawn.transform.rotationDegrees.y = 90.0f;
    doc.add(spawn);

    Entity lamp;
    lamp.id = "light_0001";
    lamp.transform.position = {0.0f, 3.0f, 0.0f};
    LightAuthor light;
    light.type = LightAuthor::Type::Point;
    light.range = 7.5f;
    lamp.light = light;
    doc.add(lamp);

    Entity sun;
    sun.id = "sun_0001";
    LightAuthor directional;
    directional.type = LightAuthor::Type::Directional;
    sun.light = directional;
    doc.add(sun);

    Entity volume;
    volume.id = "trigger_0001";
    volume.transform.position = {10.0f, 1.0f, 0.0f};
    volume.transform.rotationDegrees.y = 90.0f;
    TriggerAuthor trigger;
    trigger.size = {2.0f, 1.5f, 2.0f};
    trigger.event = "boss_gate";
    volume.trigger = trigger;
    doc.add(volume);

    Entity wall;
    wall.id = "kit.wall_0001";
    wall.prefab = "kit.wall";
    doc.add(wall);

    const std::vector<GizmoMark> marks = collectGizmoMarks(doc);

    // --- one mark per invisible thing, none for what can already be seen -----
    {
        require(has(marks, GizmoKind::PlayerSpawn), "the spawn is marked");
        require(has(marks, GizmoKind::PointLight), "the point light is marked");
        require(has(marks, GizmoKind::DirectionalLight), "and the sun");
        require(has(marks, GizmoKind::Trigger), "and the trigger volume");
        for (const GizmoMark& mark : marks)
            require(mark.id != "kit.wall_0001",
                    "a kit mesh gets no mark -- it is already visible, and a "
                    "diamond over every wall is a level nobody can read");
    }

    // --- the mark carries what makes the entity checkable at a glance -------
    {
        const GizmoMark* point = find(marks, GizmoKind::PointLight);
        require(point->radius == 7.5f,
                "a light's reach is drawn, not just written in the inspector");
        require(point->world == glm::vec3(0.0f, 3.0f, 0.0f),
                "at the light's own position");

        const GizmoMark* box = find(marks, GizmoKind::Trigger);
        require(box->halfExtents == glm::vec3(2.0f, 1.5f, 2.0f),
                 "a trigger's volume is drawn as its own box");
        require(glm::length(box->orientation *
                                glm::vec3(1.0f, 0.0f, 0.0f) -
                            glm::vec3(0.0f, 0.0f, -1.0f)) < 1e-4f,
                "a rotated trigger carries the runtime volume orientation");
        require(box->label.find("boss_gate") != std::string::npos,
                "labelled with the event it fires, which is the only thing "
                "that distinguishes two identical boxes");

        const GizmoMark* start = find(marks, GizmoKind::PlayerSpawn);
        require(start->directed &&
                    std::abs(start->yawDegrees - 90.0f) < 1e-3f,
                "the spawn shows which way the player will face");
        require(!find(marks, GizmoKind::PointLight)->directed,
                "a point light has no facing to show");
    }

    // --- every kind is told apart by colour and tag -------------------------
    {
        const GizmoKind kinds[] = {
            GizmoKind::Group,       GizmoKind::PlayerSpawn, GizmoKind::Exit,
            GizmoKind::Marker,      GizmoKind::EnemySpawn,  GizmoKind::Pickup,
            GizmoKind::Trigger,     GizmoKind::PointLight,
            GizmoKind::DirectionalLight, GizmoKind::Particles,
            GizmoKind::Collider};
        for (const GizmoKind kind : kinds) {
            const GizmoLook look = gizmoLook(kind);
            require(look.tag && look.tag[0] != '\0', "every kind has a tag");
            require((look.rgba >> 24) == 0xFF, "and is drawn opaque");
        }
    }

    // --- picking: the icon is the hit target, not the entity's metre box ----
    {
        const glm::vec2 origin(100.0f, 50.0f);
        const glm::vec2 size(800.0f, 600.0f);
        const glm::mat4 view =
            glm::lookAt(glm::vec3(0.0f, 6.0f, 20.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection =
            glm::perspective(glm::radians(60.0f), size.x / size.y, 0.05f, 400.0f);
        const glm::mat4 viewProjection = projection * view;

        // Where the light's own mark lands on screen -- clicking there must
        // select the light.
        glm::vec2 screen;
        require(projectToViewport(glm::vec3(0.0f, 3.0f, 0.0f), viewProjection,
                                  origin, size, screen),
                "the light is in front of the camera");

        const GizmoMark* hit =
            pickGizmoMark(marks, viewProjection, origin, size, screen, 12.0f);
        require(hit && hit->id == "light_0001",
                "a click on the icon selects the entity it stands for");

        const GizmoMark* miss = pickGizmoMark(marks, viewProjection, origin, size,
                                              screen + glm::vec2(200.0f, 0.0f),
                                              12.0f);
        require(!miss || miss->id != "light_0001",
                "and a click far away does not");
        require(pickGizmoMark(marks, viewProjection, origin, size, screen, 0.0f) ==
                    nullptr,
                "a zero radius hits nothing -- the caller decides how forgiving "
                "the target is");

        const std::vector<game::content::AuthorId> hidden{"light_0001"};
        require(pickGizmoMark(marks, viewProjection, origin, size, screen, 12.0f,
                              &hidden, nullptr) == nullptr,
                "a hidden mark is not pickable");
        const std::vector<game::content::AuthorId> locked{"light_0001"};
        require(pickGizmoMark(marks, viewProjection, origin, size, screen, 12.0f,
                              nullptr, &locked) == nullptr,
                "a locked mark stays visible but is not pickable");
    }

    // --- collider-only entities use the mark as their hit target ------------
    {
        Entity volume;
        volume.id = "collision_volume_0001";
        volume.collider =
            game::content::ColliderAuthor{{1.0f, 2.0f, 1.0f}, {2.0f, 0.0f, 0.0f}};
        SceneDocument boxed;
        boxed.add(volume);
        const std::vector<GizmoMark> boxes = collectGizmoMarks(boxed);
        require(boxes.size() == 1 && boxes[0].pickable,
                "a collider with no mesh or point mark supplies its own hit target");

        const glm::vec2 origin(0.0f), size(800.0f, 600.0f);
        const glm::mat4 viewProjection =
            glm::perspective(glm::radians(60.0f), size.x / size.y, 0.05f, 400.0f) *
            glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec2 screen;
        require(projectToViewport(boxes[0].world, viewProjection, origin, size,
                                  screen),
                "the offset collider is visible");
        const GizmoMark* hit =
            pickGizmoMark(boxes, viewProjection, origin, size, screen, 12.0f);
        require(hit && hit->id == volume.id,
                "clicking the visible collider-only mark selects its entity");
    }

    // --- a collider outline never steals the click --------------------------
    {
        Entity crate;
        crate.id = "crate_0001";
        crate.prefab = "kit.crate";
        crate.collider = game::content::ColliderAuthor{{0.5f, 0.5f, 0.5f}, {}};
        SceneDocument boxed;
        boxed.add(crate);

        const std::vector<GizmoMark> boxes = collectGizmoMarks(boxed);
        require(boxes.size() == 1 && boxes[0].kind == GizmoKind::Collider,
                "a collider is drawn so its extents can be checked");

        const glm::vec2 origin(0.0f), size(800.0f, 600.0f);
        const glm::mat4 viewProjection =
            glm::perspective(glm::radians(60.0f), size.x / size.y, 0.05f, 400.0f) *
            glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec2 screen;
        projectToViewport(glm::vec3(0.0f), viewProjection, origin, size, screen);
        require(pickGizmoMark(boxes, viewProjection, origin, size, screen,
                              32.0f) == nullptr,
                "but clicking it selects the mesh underneath, not the outline");
    }

    // --- a bare node is marked, and only once -------------------------------
    {
        SceneDocument nodes;
        Entity group;
        group.id = "group_0001";
        group.transform.position = {1.0f, 0.0f, 1.0f};
        nodes.add(group);

        // Something that IS a thing must not also read as an empty group.
        Entity lamp;
        lamp.id = "light_0002";
        lamp.light = LightAuthor{};
        nodes.add(lamp);

        const std::vector<GizmoMark> bare = collectGizmoMarks(nodes);
        require(has(bare, GizmoKind::Group),
                "a transform-only entity is marked -- it is the handle for "
                "everything parented to it, and it has nothing to draw");
        int groups = 0;
        for (const GizmoMark& mark : bare)
            groups += mark.kind == GizmoKind::Group ? 1 : 0;
        require(groups == 1,
                "exactly one: the lamp is a light, not an empty group");
    }

    // --- a camera is marked as a frustum, in the direction it looks ---------
    // The fov is the one authored number that cannot be judged as a number, so
    // the mark carries the shape it cuts and the facing that aims it.
    {
        SceneDocument doc;
        Entity shot;
        shot.id = "camera_0001";
        shot.transform.position = {0.0f, 2.0f, 6.0f};
        shot.transform.rotationDegrees = {0.0f, 180.0f, 0.0f};
        shot.camera = game::content::CameraAuthor{52.0f, 0.05f, 400.0f, 0, true};
        doc.add(shot);

        const std::vector<GizmoMark> marks = collectGizmoMarks(doc);
        require(has(marks, GizmoKind::Camera), "a camera entity is marked");
        const GizmoMark* mark = nullptr;
        for (const GizmoMark& candidate : marks)
            if (candidate.kind == GizmoKind::Camera)
                mark = &candidate;
        require(mark->fovDegrees == 52.0f, "the mark carries the authored fov");
        require(mark->frustumFar < 400.0f,
                "the drawn frustum is clamped: 400 m of it is two lines "
                "leaving the screen, which frames nothing");
        require(mark->volumeAlways,
                "an active camera draws its frustum at rest -- a scene has one "
                "or two, and each one IS a decision about what is on screen");
        // Facing: yaw 180 turns the camera's -Z to +Z.
        const glm::vec3 forward = mark->orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        require(forward.z > 0.9f, "the mark looks the way the entity is turned");

        doc.find("camera_0001")->camera->active = false;
        const std::vector<GizmoMark> parked = collectGizmoMarks(doc);
        for (const GizmoMark& candidate : parked)
            if (candidate.kind == GizmoKind::Camera)
                require(!candidate.volumeAlways,
                        "a parked framing is still visible, but does not lay "
                        "its frustum over the level");
    }

    // --- an orbit is marked as the ring it travels --------------------------
    // Same argument as the frustum: "radius 5.4" is a number nobody can judge,
    // and the circle it cuts next to the walls it must stay inside is the
    // decision being made.
    {
        SceneDocument doc;
        Entity rig;
        rig.id = "rig_0001";
        rig.transform.position = {4.0f, 0.0f, 0.0f};
        rig.transform.rotationDegrees.y = 90.0f;
        rig.transform.scale = {2.0f, 1.0f, 3.0f};
        doc.add(rig);
        Entity moon;
        moon.id = "moon_0001";
        moon.parent = "rig_0001";
        moon.transform.position = {2.0f, 0.0f, 0.0f};
        game::content::OrbitAuthor orbit;
        orbit.centre = {0.0f, 1.0f, 0.0f};
        orbit.radius = 3.0f;
        orbit.height = 0.5f;
        moon.orbit = orbit;
        doc.add(moon);

        const std::vector<GizmoMark> marks = collectGizmoMarks(doc);
        require(has(marks, GizmoKind::Orbit), "an orbiting entity is marked");
        const GizmoMark* mark = nullptr;
        for (const GizmoMark& candidate : marks)
            if (candidate.kind == GizmoKind::Orbit)
                mark = &candidate;
        require(mark->orbitRadius == 3.0f, "the mark carries the radius");
        require(mark->volumeAlways,
                "the ring is drawn at rest: a scene has one or two, and each "
                "is a decision about where something goes");
        // The centre is authored in the entity's own frame, so a ring inside a
        // rig has to be composed against the rig -- not drawn at the origin.
        require(std::abs(mark->orbitCentre.x - 4.0f) < 1e-4f &&
                    std::abs(mark->orbitCentre.y - 1.5f) < 1e-4f,
                "the ring centre and height are composed through the parent rig");
        require(std::abs(glm::length(mark->orbitU) - 9.0f) < 1e-4f &&
                    std::abs(glm::length(mark->orbitV) - 6.0f) < 1e-4f,
                "parent scale turns the local circle into the runtime ellipse");
        require(std::abs(mark->orbitU.x) > 8.9f,
                "parent rotation turns the orbit basis into world space");
    }

    // --- particle offsets are visible in their transformed local frame ------
    {
        SceneDocument doc;
        Entity rig;
        rig.id = "particle_rig";
        rig.transform.position = {4.0f, 1.0f, 0.0f};
        rig.transform.rotationDegrees.y = 90.0f;
        rig.transform.scale = {2.0f, 1.0f, 3.0f};
        doc.add(rig);

        Entity smoke;
        smoke.id = "smoke";
        smoke.parent = rig.id;
        game::content::ParticleAuthor particles;
        particles.effect = "treasure_dust";
        particles.offset = {1.0f, 2.0f, 0.0f};
        smoke.particles = particles;
        doc.add(smoke);

        const std::vector<GizmoMark> marks = collectGizmoMarks(doc);
        const GizmoMark* mark = find(marks, GizmoKind::Particles);
        require(mark, "a particle emitter has a viewport placement mark");
        require(mark->hasSource && mark->sourceWorld == glm::vec3(4.0f, 1.0f, 0.0f),
                "the mark retains the owning transform origin");
        require(std::abs(mark->world.x - 4.0f) < 1e-4f &&
                    std::abs(mark->world.y - 3.0f) < 1e-4f &&
                    std::abs(mark->world.z + 2.0f) < 1e-4f,
                "local offset follows parent position, rotation and scale");
        require(mark->label.find("treasure_dust") != std::string::npos,
                "the placement mark identifies the selected effect");
    }

    // --- an empty document draws nothing ------------------------------------
    require(collectGizmoMarks(SceneDocument{}).empty(),
            "no entities, no marks");

    std::cout << "EditorEntityGizmoTests: ok\n";
    return 0;
}
