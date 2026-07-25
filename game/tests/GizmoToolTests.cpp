// GizmoTool: the doodad-manipulation state machine, driven headless through its
// begin/drag/release interface with hand-built rays (no renderer, no ImGui).
// This is the payoff of lifting the drag machine out of EditorApp: the group
// translate/pivot math is now directly testable.
#include "editor/GizmoTool.h"
#include "editor/Commands.h"
#include "editor/Selection.h"

#include <eng/ecs/Components.h>

#include <cmath>
#include <cstdio>

using namespace editor;

static int failures = 0;
static void require(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool near3(glm::vec3 a, glm::vec3 b) {
    return std::fabs(a.x-b.x) < 1e-3f && std::fabs(a.y-b.y) < 1e-3f &&
           std::fabs(a.z-b.z) < 1e-3f;
}
static Ray rayDown(float x, float z = 0.0f) { // vertical ray through (x, *, z)
    Ray r; r.origin = {x, 5.0f, z}; r.dir = {0.0f, -1.0f, 0.0f}; return r;
}
static Ray rayDownThroughX(float x) { return rayDown(x, 0.0f); }

static entt::entity placed(entt::registry& r, glm::vec3 pos) {
    entt::entity e = r.create();
    eng::ecs::Transform t; t.position = pos;
    r.emplace<eng::ecs::Transform>(e, t);
    return e;
}

int main() {
    // --- begin grabs the X axis; a translate drag moves the entity along X ---
    {
        entt::registry reg;
        entt::entity e = placed(reg, glm::vec3(0.0f));
        Selection sel; sel.set(e);
        GizmoTool g;
        GizmoConfig cfg; cfg.mode = GizmoMode::Translate;

        // Grab the X axis: a ray straight down through (1,0,0) sits on the axis.
        require(g.begin(reg, sel, rayDownThroughX(1.0f), glm::vec3(0.0f), cfg),
                "begin grabs an axis under the ray");
        require(g.dragging(), "tool is dragging after a successful begin");

        // Drag so the pointer is now over x=3 -> entity translates to x=2.
        g.drag(reg, rayDownThroughX(3.0f), cfg);
        require(near3(reg.get<eng::ecs::Transform>(e).position,
                      glm::vec3(2.0f, 0.0f, 0.0f)),
                "translate drag moves the entity along the grabbed axis");

        // Release hands back a runnable, reversible command.
        Command c = g.release(reg);
        require(!g.dragging(), "release ends the drag");
        require(c.apply && c.revert, "release returns a real command");
        // release restored the pre-drag transform; the command re-applies it.
        require(near3(reg.get<eng::ecs::Transform>(e).position, glm::vec3(0.0f)),
                "release restores the pre-drag transform");
        c.apply();
        require(near3(reg.get<eng::ecs::Transform>(e).position,
                      glm::vec3(2.0f, 0.0f, 0.0f)),
                "command re-applies the drag result");
        c.revert();
        require(near3(reg.get<eng::ecs::Transform>(e).position, glm::vec3(0.0f)),
                "command reverts to the pre-drag transform");
    }

    // --- a group drag moves every selected entity by the same offset ---
    {
        entt::registry reg;
        entt::entity a = placed(reg, glm::vec3(0.0f, 0.0f, 0.0f));
        entt::entity b = placed(reg, glm::vec3(0.0f, 0.0f, 4.0f));
        Selection sel; sel.set(a); sel.add(b);
        GizmoTool g;
        GizmoConfig cfg; cfg.mode = GizmoMode::Translate;
        // Centroid of the two is (0,0,2); the X axis there passes through z=2,
        // so the grab/drag rays must sit at z=2. Primary is a. Drag to x=2.
        require(g.begin(reg, sel, rayDown(1.0f, 2.0f), glm::vec3(0, 0, 2), cfg),
                "group begin grabs an axis");
        g.drag(reg, rayDown(3.0f, 2.0f), cfg);
        require(near3(reg.get<eng::ecs::Transform>(a).position, glm::vec3(2, 0, 0)),
                "group drag moves the primary");
        require(near3(reg.get<eng::ecs::Transform>(b).position, glm::vec3(2, 0, 4)),
                "group drag moves the secondary by the same offset");
        g.release(reg);
    }

    // --- begin misses when the ray is nowhere near an axis ---
    {
        entt::registry reg;
        entt::entity e = placed(reg, glm::vec3(0.0f));
        Selection sel; sel.set(e);
        GizmoTool g;
        GizmoConfig cfg;
        Ray far; far.origin = {50.0f, 5.0f, 50.0f}; far.dir = {0, -1, 0};
        require(!g.begin(reg, sel, far, glm::vec3(0.0f), cfg),
                "begin returns false when no axis is under the ray");
        require(!g.dragging(), "no drag starts on a missed begin");
    }

    // --- translate snap rounds the result to the snap step ---
    {
        entt::registry reg;
        entt::entity e = placed(reg, glm::vec3(0.0f));
        Selection sel; sel.set(e);
        GizmoTool g;
        GizmoConfig cfg; cfg.mode = GizmoMode::Translate; cfg.snapStep = 1.0f;
        g.begin(reg, sel, rayDownThroughX(1.0f), glm::vec3(0.0f), cfg);
        g.drag(reg, rayDownThroughX(2.4f), cfg); // -> x=1.4, snaps to 1
        require(near3(reg.get<eng::ecs::Transform>(e).position, glm::vec3(1, 0, 0)),
                "snapStep rounds the translate result");
        g.release(reg);
    }

    if (failures == 0) std::printf("GizmoToolTests OK\n");
    return failures ? 1 : 0;
}
