#include "eng/Physics.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static void test_box_falls_and_rests_on_floor() {
    eng::Physics phys; phys.init();
    eng::BodyDesc floor; floor.kind = eng::ShapeKind::Box;
    floor.halfExtents = {10, 0.5f, 10}; floor.position = {0,-0.5f,0};
    floor.layer = eng::BodyLayer::Static; floor.dynamic = false;
    phys.createBody(floor);
    eng::BodyDesc box; box.kind = eng::ShapeKind::Box;
    box.halfExtents = {0.5f,0.5f,0.5f}; box.position = {0,5,0};
    box.layer = eng::BodyLayer::Prop; box.dynamic = true;
    eng::BodyHandle h = phys.createBody(box);
    for (int i = 0; i < 180; ++i) phys.update(1.0f/60.0f);
    glm::vec3 p; glm::quat q; phys.getRenderTransform(h, p, q);
    assert(std::fabs(p.y - 0.5f) < 0.05f && "box should rest on floor at y=0.5");
    phys.shutdown();
    std::puts("test_box_falls_and_rests_on_floor OK");
}
int main() { test_box_falls_and_rests_on_floor(); return 0; }
