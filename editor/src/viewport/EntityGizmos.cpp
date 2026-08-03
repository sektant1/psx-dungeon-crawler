#include <editor/viewport/EntityGizmos.h>

#include <editor/scene/Picker.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ed {
namespace {

using game::content::Entity;

std::string labelFor(const Entity& entity)
{
    return entity.name.empty() ? entity.id : entity.name;
}

bool containsId(const std::vector<game::content::AuthorId>* ids,
                const game::content::AuthorId& id)
{
    return ids && std::find(ids->begin(), ids->end(), id) != ids->end();
}

} // namespace

std::vector<GizmoMark> collectGizmoMarks(const game::content::SceneDocument& doc)
{
    std::vector<GizmoMark> marks;
    for (const Entity& entity : doc.entities) {
        // Resolved through the parent chain, like everything else the viewport
        // shows: a torch parented to a wall has to be marked where it is drawn,
        // not at the offset stored in the file.
        const game::content::WorldTransform world = doc.worldTransform(entity.id);
        const glm::vec3 worldYawPitchRoll =
            game::content::authorRotationDegrees(world.orientation);

        const std::size_t before = marks.size();
        const auto push = [&](GizmoKind kind) -> GizmoMark& {
            GizmoMark mark;
            mark.id = entity.id;
            mark.kind = kind;
            mark.world = world.position;
            mark.orientation = world.orientation;
            mark.label = labelFor(entity);
            marks.push_back(std::move(mark));
            return marks.back();
        };

        if (entity.playerSpawn) {
            GizmoMark& mark = push(GizmoKind::PlayerSpawn);
            mark.directed = true;
            mark.yawDegrees = worldYawPitchRoll.y;
        }
        if (entity.exitYawDegrees) {
            GizmoMark& mark = push(GizmoKind::Exit);
            mark.directed = true;
            // The exit's own yaw, not the entity's: it is what the runtime
            // faces the player along, and the two can differ.
            mark.yawDegrees = *entity.exitYawDegrees;
        }
        if (entity.marker)
            push(GizmoKind::Marker).label = labelFor(entity) + " (" + *entity.marker + ")";
        if (entity.enemySpawn)
            push(GizmoKind::EnemySpawn).label =
                labelFor(entity) + " (" + *entity.enemySpawn + ")";
        if (entity.pickup)
            push(GizmoKind::Pickup).label =
                labelFor(entity) + " (" + *entity.pickup + ")";
        if (entity.trigger) {
            GizmoMark& mark = push(GizmoKind::Trigger);
            mark.halfExtents = entity.trigger->size;
            if (!entity.trigger->event.empty())
                mark.label = labelFor(entity) + " -> " + entity.trigger->event;
        }
        if (entity.light) {
            const bool directional =
                entity.light->type == game::content::LightAuthor::Type::Directional;
            GizmoMark& mark = push(directional ? GizmoKind::DirectionalLight
                                               : GizmoKind::PointLight);
            // Lit in its own colour. Lighting a room is judging warm against
            // cool and bright against dim, and none of that is visible when
            // every lamp in the level is the same cyan diamond.
            mark.tinted = true;
            mark.tint = entity.light->colour;
            if (directional) {
                mark.directed = true;
                mark.yawDegrees = worldYawPitchRoll.y;
            }
            else {
                // The reach of the light, drawn as a sphere: "range 8" means
                // nothing until it is a shape you can see cross a wall.
                mark.radius = entity.light->range;
            }
        }
        if (entity.orbit) {
            // On the entity's own mark rather than a mark of its own: the ring
            // is a property of this entity's motion, and a separate mark would
            // be a second thing to click for one decision.
            const game::content::OrbitAuthor& orbit = *entity.orbit;
            GizmoMark& mark = push(GizmoKind::Orbit);
            const game::content::WorldTransform frame =
                entity.parent.empty() ? game::content::WorldTransform{}
                                      : doc.worldTransform(entity.parent);
            mark.orbitCentre = world.position;
            const float axisLength = glm::length(orbit.axis);
            if (axisLength > 0.0f) {
                const glm::vec3 axis = orbit.axis / axisLength;
                const glm::vec3 seed = std::abs(axis.x) > 0.9f
                                           ? glm::vec3(0.0f, 1.0f, 0.0f)
                                           : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 u = glm::normalize(glm::cross(seed, axis));
                const glm::vec3 v = glm::cross(axis, u);
                const auto vectorToWorld = [&frame](glm::vec3 value) {
                    return frame.orientation * (frame.scale * value);
                };
                mark.orbitRadius = orbit.radius;
                mark.orbitCentre = frame.position +
                                   vectorToWorld(orbit.centre +
                                                 axis * orbit.height);
                mark.orbitAxis = vectorToWorld(axis);
                mark.orbitU = vectorToWorld(u * orbit.radius);
                mark.orbitV = vectorToWorld(v * orbit.radius);
            }
            mark.world = mark.orbitCentre;
            mark.volumeAlways = true;
            mark.label = labelFor(entity) + "  r " +
                         std::to_string(int(orbit.radius * 10.0f + 0.5f) / 10.0f)
                             .substr(0, 4);
        }
        if (entity.camera) {
            GizmoMark& mark = push(GizmoKind::Camera);
            mark.fovDegrees = entity.camera->fovDegrees;
            mark.frustumNear = entity.camera->nearClip;
            // Not the authored far plane: 200 m of frustum drawn over a
            // dungeon is two lines running off the screen and no shot. The
            // wireframe is a framing aid, so it stops where the framing is
            // still legible.
            mark.frustumFar = std::min(entity.camera->farClip, 6.0f);
            mark.orientation = world.orientation;
            // An inactive camera is still worth seeing -- it is a parked
            // framing an author is choosing between -- but not worth a
            // full-strength frustum over the level.
            mark.volumeAlways = entity.camera->active;
            mark.label = labelFor(entity) + "  " +
                         std::to_string(int(entity.camera->fovDegrees + 0.5f)) +
                          " deg";
        }
        if (entity.viewmodelRig) {
            GizmoMark& mark = push(GizmoKind::ViewmodelSocket);
            // Camera space, composed through the entity's world transform the
            // same way a child point is: a rotated or scaled rig node must not
            // make the mark disagree with where the hands actually end up.
            mark.hasSource = true;
            mark.sourceWorld = world.position;
            mark.world +=
                world.orientation * (world.scale * entity.viewmodelRig->offset);
            mark.label = labelFor(entity) + "  hands";
        }
        if (entity.particles) {
            GizmoMark& mark = push(GizmoKind::Particles);
            mark.hasSource = true;
            mark.sourceWorld = world.position;
            // ParticleEmitter::offset is node-local at runtime. Compose it the
            // same way as a child point so a rotated or scaled parent does not
            // make the authoring mark disagree with the effect.
            mark.world += world.orientation * (world.scale * entity.particles->offset);
            if (!entity.particles->effect.empty())
                mark.label = labelFor(entity) + " (" +
                              entity.particles->effect + ")";
        }
        if (entity.audio) {
            GizmoMark& mark = push(GizmoKind::AudioEmitter);
            mark.hasSource = true;
            mark.sourceWorld = world.position;
            mark.world += world.orientation * (world.scale * entity.audio->offset);
            if (entity.audio->spatialized)
                mark.radius = entity.audio->maxDistance;
            if (!entity.audio->source.empty())
                mark.label = labelFor(entity) + " (" + entity.audio->source + ")";
        }
        if (entity.audioListener) {
            GizmoMark& mark = push(GizmoKind::AudioListener);
            mark.directed = true;
            mark.yawDegrees = worldYawPitchRoll.y;
        }
        if (entity.collider) {
            const bool hasOtherMark = marks.size() != before;
            GizmoMark& mark = push(GizmoKind::Collider);
            mark.halfExtents = entity.collider->halfExtents;
            mark.pickable = entity.prefab.empty() && !hasOtherMark;
            // Turned with the entity, the way the cooker places the collider
            // body: an offset collider on a rotated wall sits beside it, not
            // along the world axis.
            mark.world += world.orientation * entity.collider->offset;
        }

        // Nothing claimed it and it has no mesh either: a bare node. Marked
        // last so that an entity which *is* something -- a spawn, a light --
        // never also reads as an empty group.
        if (marks.size() == before && entity.prefab.empty())
            push(GizmoKind::Group);
    }
    return marks;
}

GizmoLook gizmoLook(GizmoKind kind)
{
    // ImGui packs colours ABGR, so these read backwards from the hex an artist
    // would write. They match the outliner's kind colours on purpose: one
    // vocabulary, whether the author is scanning the list or the level.
    switch (kind) {
    case GizmoKind::Group:
        return {0xFFC0B0A0, "group"};
    case GizmoKind::PlayerSpawn:
        return {0xFF9EEB8C, "spawn"};
    case GizmoKind::Exit:
        return {0xFF7FE6A0, "exit"};
    case GizmoKind::Marker:
        return {0xFFFFC7AD, "marker"};
    case GizmoKind::EnemySpawn:
        return {0xFF52A8FF, "enemy"};
    case GizmoKind::Pickup:
        return {0xFFFFCFAE, "pickup"};
    case GizmoKind::Trigger:
        return {0xFF4FADFF, "trigger"};
    case GizmoKind::PointLight:
        return {0xFF5FE0FA, "light"};
    case GizmoKind::DirectionalLight:
        return {0xFF73E0FA, "sun"};
    case GizmoKind::Camera:
        return {0xFFEFE0B0, "camera"};
    case GizmoKind::ViewmodelSocket:
        return {0xFF9CD8FF, "hands"};
    case GizmoKind::Orbit:
        return {0xFFB0D8FF, "orbit"};
    case GizmoKind::Particles:
        return {0xFFFFB45C, "particles"};
    case GizmoKind::AudioEmitter:
        return {0xFFF2C14E, "audio"};
    case GizmoKind::AudioListener:
        return {0xFFB5E853, "listener"};
    case GizmoKind::Collider:
        return {0xFF8F8F8F, "collider"};
    }
    return {0xFFB0A099, "?"};
}

namespace {

// Dims a colour without changing its hue, for the marks that are not selected.
unsigned fade(unsigned rgba, float alpha)
{
    const unsigned a = unsigned(float((rgba >> 24) & 0xFF) * alpha);
    return (rgba & 0x00FFFFFFu) | (a << 24);
}

// An authored light colour, as something ImGui can draw.
//
// Light colours carry energy: a torch is authored well above 1.0 so the bloom
// pass catches it. Normalising by the brightest channel keeps the *hue* -- warm
// or cold, which is the decision being looked at -- instead of clipping every
// strong light to white and losing exactly the distinction the mark exists to
// show. The floor keeps a near-black light visible at all.
unsigned packTint(const glm::vec3& colour)
{
    const float peak = std::max({colour.r, colour.g, colour.b, 1e-3f});
    const glm::vec3 hue = colour / std::max(peak, 1.0f);
    const auto channel = [](float value) {
        return unsigned(std::clamp(value, 0.10f, 1.0f) * 255.0f) & 0xFFu;
    };
    return 0xFF000000u | (channel(hue.b) << 16) | (channel(hue.g) << 8) |
           channel(hue.r);
}

bool project(const GizmoOverlay& overlay, glm::vec3 world, ImVec2& out)
{
    glm::vec2 screen;
    if (!projectToViewport(world, *overlay.viewProjection,
                           overlay.viewportOrigin, overlay.viewportSize, screen))
        return false;
    out = ImVec2(screen.x, screen.y);
    return true;
}

// The eight corners of a box, projected. False when any corner is behind the
// camera: a box with one corner behind the eye projects to a shape that sweeps
// across the whole screen, which reads as a rendering bug.
bool projectBox(const GizmoOverlay& overlay, glm::vec3 centre, glm::vec3 half,
                const glm::quat& orientation, ImVec2 out[8])
{
    static const glm::vec3 kSigns[8] = {
        {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1},
        {-1, 1, -1},  {1, 1, -1},  {1, 1, 1},  {-1, 1, 1},
    };
    for (int i = 0; i < 8; ++i)
        if (!project(overlay,
                     centre + orientation * (kSigns[i] * half), out[i]))
            return false;
    return true;
}

void drawBox(ImDrawList* list, const ImVec2 corners[8], unsigned colour,
             float thickness)
{
    static const int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                      {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                      {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : kEdges)
        list->AddLine(corners[edge[0]], corners[edge[1]], colour, thickness);
}

// One great circle of a sphere, in the plane spanned by `u` and `v`.
void drawCircle(ImDrawList* list, const GizmoOverlay& overlay, glm::vec3 centre,
                float radius, glm::vec3 u, glm::vec3 v, unsigned colour,
                float thickness)
{
    constexpr int kSegments = 40;
    ImVec2 previous;
    bool havePrevious = false;
    for (int i = 0; i <= kSegments; ++i) {
        const float t = float(i) / float(kSegments) * 6.2831853f;
        const glm::vec3 point =
            centre + (u * std::cos(t) + v * std::sin(t)) * radius;
        ImVec2 screen;
        if (!project(overlay, point, screen)) {
            havePrevious = false;
            continue;
        }
        if (havePrevious)
            list->AddLine(previous, screen, colour, thickness);
        previous = screen;
        havePrevious = true;
    }
}

// A light's reach. Three great circles rather than one ground ring: a lamp on a
// wall two metres up throws a sphere, and a circle drawn on the floor under it
// says nothing about whether it reaches the ceiling or the far side of a
// doorway. The ground circle stays -- it is the one that reads against the
// floor plan -- and the two vertical ones give the volume.
void drawWireSphere(ImDrawList* list, const GizmoOverlay& overlay,
                    glm::vec3 centre, float radius, unsigned colour)
{
    const glm::vec3 x(1.0f, 0.0f, 0.0f);
    const glm::vec3 y(0.0f, 1.0f, 0.0f);
    const glm::vec3 z(0.0f, 0.0f, 1.0f);
    drawCircle(list, overlay, centre, radius, x, z, colour, 1.5f); // ground
    drawCircle(list, overlay, centre, radius, x, y, fade(colour, 0.55f), 1.0f);
    drawCircle(list, overlay, centre, radius, z, y, fade(colour, 0.55f), 1.0f);
}

// The volume a camera sees: the near rectangle, the far rectangle, and the four
// rays joining them.
//
// This is the fov made visible. "60 degrees" is a number an author cannot
// judge; the wedge it cuts through the room, next to the prop it is supposed to
// frame, is the decision itself. Drawn in the camera's own basis, so a shot
// that is pitched down shows as pitched down.
void drawFrustum(ImDrawList* list, const GizmoOverlay& overlay,
                 const GizmoMark& mark, unsigned colour, bool active)
{
    const float halfHeightNear =
        std::tan(glm::radians(mark.fovDegrees) * 0.5f) * mark.frustumNear;
    const float halfHeightFar =
        std::tan(glm::radians(mark.fovDegrees) * 0.5f) * mark.frustumFar;
    // -Z forward, +Y up: the renderer's convention, so what the gizmo draws and
    // what the camera renders cannot disagree.
    const glm::vec3 right = mark.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = mark.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = mark.orientation * glm::vec3(0.0f, 0.0f, -1.0f);

    const float aspect = overlay.aspect > 0.0f ? overlay.aspect : mark.aspect;
    const auto corner = [&](float distance, float halfHeight, float sx,
                            float sy) {
        return mark.world + forward * distance +
               right * (halfHeight * aspect * sx) + up * (halfHeight * sy);
    };
    ImVec2 nearScreen[4];
    ImVec2 farScreen[4];
    static const float kSigns[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    for (int i = 0; i < 4; ++i) {
        if (!project(overlay,
                     corner(mark.frustumNear, halfHeightNear, kSigns[i][0],
                            kSigns[i][1]),
                     nearScreen[i]))
            return; // any corner behind the eye and the shape is meaningless
        if (!project(overlay,
                     corner(mark.frustumFar, halfHeightFar, kSigns[i][0],
                            kSigns[i][1]),
                     farScreen[i]))
            return;
    }

    const float thickness = active ? 2.0f : 1.0f;
    // The far rectangle is the frame: brightest, and filled faintly so the shot
    // reads as a plane rather than four more lines over the level.
    list->AddConvexPolyFilled(farScreen, 4, fade(colour, active ? 0.16f : 0.08f));
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        list->AddLine(farScreen[i], farScreen[next], colour, thickness);
        list->AddLine(nearScreen[i], nearScreen[next], fade(colour, 0.6f), 1.0f);
        list->AddLine(nearScreen[i], farScreen[i], fade(colour, 0.75f), thickness);
    }
    // Up, so a rolled or upside-down camera is visible before it is played.
    ImVec2 apex;
    const float halfWidthFar = halfHeightFar * aspect;
    if (project(overlay,
                mark.world + forward * mark.frustumFar +
                    up * (halfHeightFar * 1.45f),
                apex)) {
        const ImVec2 tri[3] = {farScreen[0], farScreen[1], apex};
        list->AddTriangleFilled(tri[0], tri[1], tri[2], fade(colour, 0.35f));
        list->AddTriangle(tri[0], tri[1], tri[2], colour, 1.0f);
    }
    (void)halfWidthFar;
}

// The camera body: a small filled box with the lens wedge pointing along the
// shot, matching the outliner's icon so one vocabulary covers both panels.
void drawCameraBody(ImDrawList* list, ImVec2 centre, unsigned colour,
                    float radius, bool active, const ImVec2& towards)
{
    const float dx = towards.x - centre.x;
    const float dy = towards.y - centre.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    // Facing unknown (the camera points at the eye): a plain box, which is the
    // honest drawing of a shot pointing straight at you.
    const glm::vec2 dir = length > 1e-3f ? glm::vec2(dx / length, dy / length)
                                         : glm::vec2(0.0f);
    const glm::vec2 side(-dir.y, dir.x);
    const auto at = [&](float along, float across) {
        return ImVec2(centre.x + dir.x * along * radius + side.x * across * radius,
                      centre.y + dir.y * along * radius + side.y * across * radius);
    };
    const ImVec2 body[4] = {at(-0.9f, -0.7f), at(0.5f, -0.7f), at(0.5f, 0.7f),
                            at(-0.9f, 0.7f)};
    list->AddConvexPolyFilled(body, 4, fade(colour, active ? 0.75f : 0.45f));
    list->AddPolyline(body, 4, colour, ImDrawFlags_Closed, active ? 2.0f : 1.5f);
    if (length > 1e-3f) {
        const ImVec2 lens[4] = {at(0.5f, -0.5f), at(1.5f, -1.0f), at(1.5f, 1.0f),
                                at(0.5f, 0.5f)};
        list->AddConvexPolyFilled(lens, 4, fade(colour, active ? 0.85f : 0.55f));
        list->AddPolyline(lens, 4, colour, ImDrawFlags_Closed, 1.5f);
    }
}

// The ring an orbiting entity travels, with a tick at the centre it circles.
//
// The same argument as the camera frustum: "radius 5.4" is a number nobody can
// judge, and the circle it cuts next to the walls it has to stay inside is the
// decision. The centre tick matters as much -- an orbit whose centre drifted
// off the subject looks, from a still frame, exactly like one that did not.
void drawOrbitRing(ImDrawList* list, const GizmoOverlay& overlay,
                   const GizmoMark& mark, unsigned colour, bool active)
{
    if (glm::length(mark.orbitU) <= 0.0f || glm::length(mark.orbitV) <= 0.0f)
        return;
    drawCircle(list, overlay, mark.orbitCentre, 1.0f, mark.orbitU, mark.orbitV,
               colour, active ? 2.0f : 1.0f);

    // The centre, as a cross rather than a dot: a dot on a busy floor reads as
    // a speck of texture.
    ImVec2 centre;
    if (!project(overlay, mark.orbitCentre, centre))
        return;
    const float arm = active ? 6.0f : 4.0f;
    list->AddLine(ImVec2(centre.x - arm, centre.y),
                  ImVec2(centre.x + arm, centre.y), colour, 1.5f);
    list->AddLine(ImVec2(centre.x, centre.y - arm),
                  ImVec2(centre.x, centre.y + arm), colour, 1.5f);
}

// The bulb: a filled core with two haloes around it, in the light's own colour.
//
// A light is the one entity whose mark should look like what it does. This is
// the difference between "there is a light entity here" and "this corner is lit
// warm and that one cold", which is the actual question being answered when
// somebody lights a room.
void drawGlow(ImDrawList* list, ImVec2 centre, unsigned colour, float radius,
              bool active)
{
    list->AddCircleFilled(centre, radius * 2.6f, fade(colour, 0.10f), 20);
    list->AddCircleFilled(centre, radius * 1.7f, fade(colour, 0.18f), 20);
    list->AddCircleFilled(centre, radius, fade(colour, 0.85f), 16);
    // A dark rim, so a pale lamp stays legible against a pale floor.
    list->AddCircle(centre, radius, 0xB0000000, 16, active ? 2.0f : 1.0f);
}

} // namespace

void drawGizmoMarks(ImDrawList* list, const std::vector<GizmoMark>& marks,
                    const GizmoOverlay& overlay)
{
    if (!list || !overlay.viewProjection)
        return;

    for (const GizmoMark& mark : marks) {
        if (containsId(overlay.hidden, mark.id))
            continue;
        const bool isSelected =
            overlay.selected &&
            std::find(overlay.selected->begin(), overlay.selected->end(),
                      mark.id) != overlay.selected->end();
        const bool isHovered = overlay.hovered && *overlay.hovered == mark.id;
        // "Attended to": selected, or under the cursor. Everything that makes a
        // mark loud is spent here, and only here.
        const bool active = isSelected || isHovered;
        const GizmoLook look = gizmoLook(mark.kind);
        // A light wears its own colour; everything else wears its kind's, which
        // is what keeps the outliner's tags and the viewport's marks one
        // vocabulary.
        const unsigned base = mark.tinted ? packTint(mark.tint) : look.rgba;
        const unsigned colour = active ? base : fade(base, 0.62f);

        ImVec2 centre;
        if (!project(overlay, mark.world, centre))
            continue;

        if (mark.hasSource && active) {
            ImVec2 source;
            if (project(overlay, mark.sourceWorld, source)) {
                list->AddLine(source, centre, fade(colour, 0.8f), 1.5f);
                list->AddCircleFilled(source, 2.5f, fade(colour, 0.8f), 8);
            }
        }

        if (overlay.volumes && mark.halfExtents != glm::vec3(0.0f)) {
            ImVec2 corners[8];
            if (projectBox(overlay, mark.world, mark.halfExtents,
                           mark.orientation, corners))
                drawBox(list, corners, active ? colour : fade(colour, 0.5f),
                        active ? 2.0f : 1.0f);
        }
        // A light's reach is a big sphere, and a room with eight lamps in it is
        // eight overlapping spheres that bury the level they are describing.
        // Drawn only for the light being worked on: at rest the author needs
        // "there is a lamp here", and the reach when they are tuning it.
        if (overlay.volumes && mark.radius > 0.0f && active)
            drawWireSphere(list, overlay, mark.world, mark.radius,
                           fade(colour, 0.75f));
        // A frustum is drawn at rest, unlike a light's reach: a scene has one
        // or two cameras and each one IS a decision about what is on screen,
        // where a room has eight lamps whose spheres would bury it.
        if (overlay.volumes && mark.orbitRadius > 0.0f &&
            (active || mark.volumeAlways))
            drawOrbitRing(list, overlay, mark,
                          active ? colour : fade(colour, 0.55f), active);
        if (overlay.volumes && mark.fovDegrees > 0.0f &&
            (active || mark.volumeAlways))
            drawFrustum(list, overlay, mark, active ? colour : fade(colour, 0.7f),
                        active);

        const float r = active ? 7.0f : 5.0f;
        if (mark.kind == GizmoKind::Camera) {
            // Aimed at where the shot goes, so the body points the way the
            // frustum opens even when the frustum itself is off screen.
            const glm::vec3 forward =
                mark.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
            ImVec2 towards = centre;
            project(overlay, mark.world + forward, towards);
            drawCameraBody(list, centre, colour, r, active, towards);
        } else if (mark.tinted) {
            drawGlow(list, centre, colour, r * 0.85f, active);
        } else {
            // A diamond rather than a circle: at this size a circle is a blob,
            // and the four points survive being drawn over a busy stone
            // texture.
            const ImVec2 diamond[4] = {
                ImVec2(centre.x, centre.y - r), ImVec2(centre.x + r, centre.y),
                ImVec2(centre.x, centre.y + r), ImVec2(centre.x - r, centre.y)};
            list->AddConvexPolyFilled(diamond, 4, fade(colour, 0.35f));
            list->AddPolyline(diamond, 4, colour, ImDrawFlags_Closed,
                              active ? 2.0f : 1.5f);
        }

        if (mark.directed) {
            // Yaw is measured the way the authored transform measures it, so
            // the arrow and the runtime always agree about "forward".
            const float yaw = glm::radians(mark.yawDegrees);
            const glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
            ImVec2 tip;
            if (project(overlay, mark.world + forward * 1.5f, tip))
                list->AddLine(centre, tip, colour, active ? 2.0f : 1.5f);
        }

        // Labels only for what is attended to. Thirty marks in a room is thirty
        // words of text over the level, all of them overlapping, and the result
        // is that none of them can be read -- including the one being looked
        // for. The diamonds stay, so nothing is hidden; the words arrive when
        // the cursor does.
        if (overlay.labels && active) {
            const ImVec2 at(centre.x + r + 4.0f, centre.y - 7.0f);
            const auto shadowed = [list](ImVec2 pos, unsigned rgba,
                                         const char* text) {
                // Shadowed, because the label sits over whatever the level
                // happens to be and half of this dungeon is the same value as
                // the text.
                list->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), 0xC0000000,
                              text);
                list->AddText(pos, rgba, text);
            };
            shadowed(at, colour, look.tag);
            shadowed(ImVec2(at.x, at.y + ImGui::GetTextLineHeight()), colour,
                     mark.label.c_str());
        }
    }
}

const GizmoMark* pickGizmoMark(const std::vector<GizmoMark>& marks,
                               const glm::mat4& viewProjection,
                               glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                               glm::vec2 point, float radius,
                               const std::vector<game::content::AuthorId>* hidden,
                               const std::vector<game::content::AuthorId>* locked)
{
    const GizmoMark* best = nullptr;
    float bestDistance = radius;
    for (const GizmoMark& mark : marks) {
        if (!mark.pickable || containsId(hidden, mark.id) ||
            containsId(locked, mark.id))
            continue;
        glm::vec2 screen;
        if (!projectToViewport(mark.world, viewProjection, viewportOrigin,
                               viewportSize, screen))
            continue;
        const float distance = glm::length(screen - point);
        // Strictly nearer, so the first mark of a tie wins and the choice does
        // not depend on entity order changing under an edit.
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &mark;
        }
    }
    return best;
}

} // namespace ed
