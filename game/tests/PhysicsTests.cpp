#include "eng/Physics.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>
#include <glm/gtc/quaternion.hpp>

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

static void test_raycast_hits_static_box() {
    eng::Physics phys; phys.init();
    eng::BodyDesc wall; wall.halfExtents = {0.5f,2,2};
    wall.position = {3,0,0}; wall.layer = eng::BodyLayer::Static; wall.dynamic = false;
    eng::BodyHandle wh = phys.createBody(wall);
    eng::RayHit hit;
    bool ok = phys.rayCast({0,0,0}, {1,0,0}, 10.0f, hit, eng::BodyLayer::Static);
    assert(ok && "ray should hit wall");
    assert(hit.body == wh && "ray should report the wall body");
    assert(std::fabs(hit.point.x - 2.5f) < 0.05f && "hit at wall's near face");
    phys.shutdown();
    std::puts("test_raycast_hits_static_box OK");
}

static void test_mesh_body_is_solid() {
    eng::Physics phys; phys.init();
    // Two-triangle 20x20 quad at y=0 (CCW winding, normal up).
    std::vector<glm::vec3> verts = {{-10,0,-10},{10,0,-10},{10,0,10},{-10,0,10}};
    std::vector<uint32_t> idx = {0,2,1, 0,3,2};
    phys.createMeshBody(verts, idx, {0,0,0}, glm::quat(1,0,0,0), eng::BodyLayer::Static);
    eng::BodyDesc box; box.halfExtents={0.5f,0.5f,0.5f}; box.position={0,5,0};
    box.layer=eng::BodyLayer::Prop; box.dynamic=true;
    eng::BodyHandle b = phys.createBody(box);
    for (int i=0;i<180;++i) phys.update(1.0f/60.0f);
    glm::vec3 p; glm::quat q; phys.getRenderTransform(b,p,q);
    assert(std::fabs(p.y-0.5f)<0.06f && "box should rest on the mesh floor");
    phys.shutdown();
    std::puts("test_mesh_body_is_solid OK");
}

static void test_character_settles_and_is_blocked_by_wall() {
    eng::Physics phys; phys.init();
    eng::BodyDesc floor; floor.halfExtents={20,0.5f,20}; floor.position={0,-0.5f,0};
    floor.layer=eng::BodyLayer::Static; floor.dynamic=false; phys.createBody(floor);
    eng::BodyDesc wall; wall.halfExtents={0.5f,2,10}; wall.position={2,2,0};
    wall.layer=eng::BodyLayer::Static; wall.dynamic=false; phys.createBody(wall);
    eng::CharacterDesc cd; cd.position={0,1.0f,0};
    eng::CharacterHandle ch = phys.createCharacter(cd);
    for (int i=0;i<120;++i){ phys.characterSetVelocity(ch,{5,0,0}); phys.characterUpdate(ch,1.0f/60.0f); phys.update(1.0f/60.0f); }
    eng::CharacterState s = phys.characterState(ch);
    assert(s.grounded() && "character should be grounded on floor");
    assert(s.position.x < 1.5f && "character must not pass through the wall at x=1.5");
    phys.shutdown();
    std::puts("test_character_settles_and_is_blocked_by_wall OK");
}
static void test_character_steps_small_ledge() {
    eng::Physics phys; phys.init();
    eng::BodyDesc floor; floor.halfExtents={20,0.5f,20}; floor.position={0,-0.5f,0};
    floor.layer=eng::BodyLayer::Static; floor.dynamic=false; phys.createBody(floor);
    eng::BodyDesc step; step.halfExtents={5,0.15f,5}; step.position={5,0.15f,0}; // 0.30 m tall
    step.layer=eng::BodyLayer::Static; step.dynamic=false; phys.createBody(step);
    eng::CharacterDesc cd; cd.position={0,1.0f,0}; cd.stepHeight=0.4f;
    eng::CharacterHandle ch = phys.createCharacter(cd);
    for (int i=0;i<240;++i){ phys.characterSetVelocity(ch,{4,0,0}); phys.characterUpdate(ch,1.0f/60.0f); phys.update(1.0f/60.0f); }
    assert(phys.characterState(ch).position.y > 0.25f && "character should have stepped up onto the ledge");
    phys.shutdown();
    std::puts("test_character_steps_small_ledge OK");
}

int main() {
    test_box_falls_and_rests_on_floor();
    test_raycast_hits_static_box();
    test_mesh_body_is_solid();
    test_character_settles_and_is_blocked_by_wall();
    test_character_steps_small_ledge();
    return 0;
}
