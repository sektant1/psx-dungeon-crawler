#pragma once
#include <eng/DebugTools.h> // eng::DebugTools, the panel host this joins

#include <entt/entt.hpp>

namespace eng::ecs {
class World;
class ComponentRegistry;
}

namespace eng {

// The timeline: browse the entities carrying a `Clip`, scrub one, add tracks,
// and drag keyframes.
//
// It lives in the engine, beside ParticlePanel and for the same reason:
// everything it touches -- eng::ecs::Clip, the ComponentRegistry, clipSystem --
// is engine machinery, and an application that reimplemented this would be
// reimplementing engine tooling. The scene editor and the game both get it by
// constructing one and calling install().
//
// Why the track picker has no hardcoded list of animatable things. A track
// names a component and a field as strings and the ComponentRegistry resolves
// them, so the two dropdowns in "Add track" are *the registry*, filtered to the
// field types a clip can interpolate. A component reflected tomorrow appears in
// them tomorrow with no edit here -- which is the same property that makes it
// animatable at all (see eng/ecs/components/Clip.h).
//
// Two things are deliberately not here. There is no curve editor: a key holds a
// value and a track holds one easing, which covers the door/platform/light/push
// -in cases the clip player exists for, and a bezier handle per key is the
// beginning of the second animation runtime the component's comment refuses.
// And there is no save button -- the panel edits the live component, and the
// scene editor's own save is what writes a `.scn`. In the *game* the edits are
// a tuning session and are meant to be lost, exactly like every other slider in
// this console.
class ClipPanel {
public:
    // Refreshed by the application every frame, so nothing dangles across a
    // level rebuild. Either may be null; the panel then says so rather than
    // dereferencing it. `types` is normally the same registry the World was
    // given via World::setComponentTypes -- the panel reads it directly because
    // it must list types even for a World that has none attached yet.
    void setSources(ecs::World* world, const ecs::ComponentRegistry* types);

    // Registers the tab in the debug console. Call once at startup. What the
    // game uses.
    void install(DebugTools& tools, PanelGroup group = PanelGroup::World);

    // The panel's contents, without a window or a tab around them. For a host
    // that owns its own docking -- the scene editor puts this in a Timeline
    // window along the bottom, where a timeline belongs and where a console tab
    // cannot go.
    void drawBody() { draw(); }

    // The entity whose clip is shown, or entt::null. Exposed so an editor can
    // keep the timeline's selection in step with the outliner's.
    //
    // Setting it goes through a function rather than assigning the member,
    // because changing the selection has to drop any drag in progress -- see
    // the definition.
    entt::entity selected() const { return mSelected; }
    void select(entt::entity e) { setSelected(e); }

private:
    void setSelected(entt::entity e);
    void draw();
    void drawEntityList();
    void drawTransport();
    void drawTimeline();
    void drawAddTrack();

    ecs::World* mWorld = nullptr;
    const ecs::ComponentRegistry* mTypes = nullptr;
    entt::entity mSelected = entt::null;

    // Which track/key the mouse is dragging, or -1. Held across frames because
    // a drag is a gesture and ImGui reports it one frame at a time.
    int mDragTrack = -1;
    int mDragKey = -1;
    // Pixels per second. A clip is under a couple of seconds by construction,
    // so the default fits one on screen without scrolling.
    float mZoom = 320.0f;
    // The "Add track" picker's current choice, as indices into the registry.
    int mPickType = -1;
    int mPickField = -1;
    char mPickTarget[64] = {};
};

} // namespace eng
