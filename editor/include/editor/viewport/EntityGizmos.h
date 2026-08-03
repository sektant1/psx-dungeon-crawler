#pragma once
#include <editor/content/SceneDocument.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

// Declared at global scope on purpose: inside `namespace ed` this would name a
// distinct ed::ImDrawList, and every call would fail on an incomplete type.
struct ImDrawList;

namespace ed {

// Editor-only visuals for the parts of a level that have no mesh.
//
// Half of what a level *is* -- where the player starts, where the exit leads,
// what a trigger covers, how far a light reaches -- was invisible in the
// viewport. Those entities were in the outliner and they could be clicked if
// you happened to guess where they stood, which meant the only reliable way to
// place a spawn was to cook the map and run the game.
//
// The visuals are drawn in screen space over the viewport image, not as world
// geometry: the rendered image is a shipped result and no editor affordance may
// change what the renderer produces. Projection is the picker's own, so a mark
// is always drawn exactly where a click on it would land.

enum class GizmoKind {
    // A bare transform: no mesh, no components, only a place and whatever is
    // parented to it. Without a mark it would be invisible in the viewport and
    // reachable only from the outliner -- and it is the entity most likely to
    // be the handle for everything around it.
    Group,
    PlayerSpawn,
    Exit,
    Marker,
    EnemySpawn,
    Pickup,
    Trigger,
    PointLight,
    DirectionalLight,
    // A point of view. The one gizmo whose *shape* is the authored value: a
    // camera's fov is a number nobody can judge, and the same number drawn as
    // the volume it will see is a framing decision an author can make by
    // looking at it.
    Camera,
    // The ring an entity travels. Drawn on the entity's own mark, because the
    // ring is a property of its motion rather than a thing of its own.
    Orbit,
    // The point where a ParticleEmitter emits after its local offset is
    // transformed through the entity hierarchy.
    Particles,
    // Positional clip origin and its maximum audible distance.
    AudioEmitter,
    // Runtime listener candidate on a transform-only rig node.
    AudioListener,
    Collider,
};

struct GizmoMark {
    game::content::AuthorId id;
    GizmoKind kind = GizmoKind::Marker;
    glm::vec3 world{0.0f};
    std::string label;
    // Wire box, in metres, centred on `world`. Zero when the mark has none.
    glm::vec3 halfExtents{0.0f};
    // World orientation for authored boxes and directed volumes. Keeping this
    // beside the extents prevents rotated trigger/collider gizmos from lying
    // about the volume used by runtime physics.
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    // Reach, in metres: a light's range, drawn as a wire sphere. Zero when the
    // mark has none.
    float radius = 0.0f;
    // The entity's own colour, when it has one. A light authored deep red and a
    // light authored pale blue are different level design decisions, and a
    // panel of identical cyan diamonds cannot show which is which -- the whole
    // job of lighting a room is judging colours against each other.
    bool tinted = false;
    glm::vec3 tint{1.0f};
    // Facing, for the marks where "which way" is part of the authoring: an exit
    // the player arrives through backwards is a bug you can only see in game.
    bool directed = false;
    float yawDegrees = 0.0f;
    // A camera's frustum, in the entity's own frame. Zero fov means the mark
    // has none. The orientation is carried whole rather than as a yaw, because
    // a camera that is only ever level is not a camera anybody would use.
    float fovDegrees = 0.0f;
    float frustumNear = 0.0f;
    float frustumFar = 0.0f;
    // 16:9 unless the caller says otherwise: the frustum is a framing aid and
    // has to match the aspect the shot will actually be composed at.
    float aspect = 16.0f / 9.0f;
    // Drawn even when the mark is not selected, for the marks whose volume IS
    // the information. A room of light spheres buries the level; one camera
    // frustum is the shot.
    bool volumeAlways = false;
    // An orbit ring, in the entity's own frame. Zero radius means the mark has
    // none. Like the camera's fov, the numbers behind it -- a centre, a radius,
    // an inclination -- cannot be judged as numbers; the circle they cut
    // through the room is the decision itself.
    float orbitRadius = 0.0f;
    glm::vec3 orbitCentre{0.0f};
    glm::vec3 orbitAxis{0.0f, 1.0f, 0.0f};
    // World-space radius vectors. They preserve parent rotation and scale, so
    // a local circular orbit correctly appears as an ellipse under a scaled rig.
    glm::vec3 orbitU{0.0f};
    glm::vec3 orbitV{0.0f};
    // Optional line back to the owning entity's transform origin. Particle
    // offsets are much easier to place when both ends are visible together.
    bool hasSource = false;
    glm::vec3 sourceWorld{0.0f};
    // Collider outlines normally defer to the mesh below them. A collider-only
    // entity has no other hit target, so its visible mark must remain pickable.
    bool pickable = true;
};

// Every mark the document implies. An entity can produce more than one -- a
// trigger volume with a collider is a box and a box -- and a plain kit mesh
// produces none, because it can already be seen.
std::vector<GizmoMark> collectGizmoMarks(const game::content::SceneDocument& doc);

struct GizmoLook {
    unsigned rgba;    // ImGui's packed ABGR, ready for ImDrawList
    const char* tag;  // short kind name, drawn beside the mark
};
GizmoLook gizmoLook(GizmoKind kind);

// The mark nearest `point` within `radius` screen pixels, or null. Marks behind
// the camera never match.
//
// This is what makes the invisible half of a level clickable: the entity's own
// bounds are a metre box in the middle of a room, so hitting it with a ray is
// luck, while its icon is exactly where the eye already is.
struct GizmoOverlay {
    const glm::mat4* viewProjection = nullptr;
    glm::vec2 viewportOrigin{0.0f};
    glm::vec2 viewportSize{0.0f};
    // Drawn brighter, labelled, and with their volumes at full strength, so a
    // selection made in the outliner can be found in the level.
    const std::vector<game::content::AuthorId>* selected = nullptr;
    const std::vector<game::content::AuthorId>* hidden = nullptr;
    const std::vector<game::content::AuthorId>* locked = nullptr;
    // The mark under the cursor, if any. It gets the same treatment as a
    // selected one, which is what makes the marks explorable: a level with
    // thirty of them cannot label them all at once and stay readable.
    const game::content::AuthorId* hovered = nullptr;
    bool labels = true;
    bool volumes = true;
    // Width/height a camera frustum is drawn at. Zero uses each mark's own.
    // The viewport's, in the editor: an author framing a shot has to be judging
    // the rectangle they are looking through, not a nominal 16:9.
    float aspect = 0.0f;
};

// Draws the marks over the viewport image. `list` is an ImGui draw list --
// forward-declared so this header stays usable from the headless tests.
void drawGizmoMarks(ImDrawList* list, const std::vector<GizmoMark>& marks,
                    const GizmoOverlay& overlay);

const GizmoMark* pickGizmoMark(const std::vector<GizmoMark>& marks,
                               const glm::mat4& viewProjection,
                               glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                               glm::vec2 point, float radius,
                               const std::vector<game::content::AuthorId>* hidden = nullptr,
                               const std::vector<game::content::AuthorId>* locked = nullptr);

} // namespace ed
