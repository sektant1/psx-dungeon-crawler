#include "eng/Physics.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <glm/gtc/quaternion.hpp>

// CHECK() is compiled out under -DNDEBUG (Release), which would make every
// check below a no-op and the suite vacuously "pass". CHECK aborts in any
// build configuration so these tests actually verify behaviour.
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", msg,     \
                         __FILE__, __LINE__);                               \
            std::abort();                                                   \
        }                                                                   \
    } while (0)

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
    CHECK(std::fabs(p.y - 0.5f) < 0.05f, "box should rest on floor at y=0.5");
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
    CHECK(ok, "ray should hit wall");
    CHECK(hit.body == wh, "ray should report the wall body");
    CHECK(std::fabs(hit.point.x - 2.5f) < 0.05f, "hit at wall's near face");
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
    CHECK(std::fabs(p.y-0.5f)<0.06f, "box should rest on the mesh floor");
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
    // CharacterVirtual does not integrate gravity itself: the caller feeds it a
    // velocity that includes accumulated fall speed (as FpsController does).
    float vy = 0.0f;
    for (int i=0;i<180;++i){
        vy -= 18.0f/60.0f;
        if (phys.characterState(ch).grounded() && vy < 0) vy = 0;
        phys.characterSetVelocity(ch,{5,vy,0});
        phys.characterUpdate(ch,1.0f/60.0f);
        phys.update(1.0f/60.0f);
    }
    eng::CharacterState s = phys.characterState(ch);
    CHECK(s.grounded(), "character should be grounded on floor");
    CHECK(s.position.x < 1.5f, "character must not pass through the wall at x=1.5");
    phys.shutdown();
    std::puts("test_character_settles_and_is_blocked_by_wall OK");
}
static void test_character_steps_small_ledge() {
    eng::Physics phys; phys.init();
    eng::BodyDesc floor; floor.halfExtents={20,0.5f,20}; floor.position={0,-0.5f,0};
    floor.layer=eng::BodyLayer::Static; floor.dynamic=false; phys.createBody(floor);
    // Leave a clear run-up. The previous box began at x=0, overlapping the
    // character's spawn and testing penetration recovery rather than stairs.
    // Ledge spans x=2..20 so the character climbs at x=2 and stays on it (a
    // short ledge lets a 4 m/s walk overshoot the far edge back to the floor).
    eng::BodyDesc step; step.halfExtents={9,0.15f,5}; step.position={11,0.15f,0}; // begins x=2, 0.30 m tall
    step.layer=eng::BodyLayer::Static; step.dynamic=false; phys.createBody(step);
    eng::CharacterDesc cd; cd.position={0,1.0f,0}; cd.stepHeight=0.4f;
    eng::CharacterHandle ch = phys.createCharacter(cd);
    float vy = 0.0f;
    float maxY = cd.position.y;
    for (int i=0;i<300;++i){
        vy -= 18.0f/60.0f;
        if (phys.characterState(ch).grounded() && vy < 0) vy = 0;
        phys.characterSetVelocity(ch,{4,vy,0});
        phys.characterUpdate(ch,1.0f/60.0f);
        phys.update(1.0f/60.0f);
        maxY = std::max(maxY, phys.characterState(ch).position.y);
    }
    CHECK(maxY > 0.25f, "character should have stepped up onto the ledge");
    phys.shutdown();
    std::puts("test_character_steps_small_ledge OK");
}

static void test_impulse_moves_prop() {
    eng::Physics phys; phys.init();
    eng::BodyDesc floor; floor.halfExtents={20,0.5f,20}; floor.position={0,-0.5f,0};
    floor.layer=eng::BodyLayer::Static; floor.dynamic=false; phys.createBody(floor);
    eng::BodyDesc crate; crate.halfExtents={0.4f,0.4f,0.4f}; crate.position={0,0.4f,0};
    crate.layer=eng::BodyLayer::Prop; crate.mass=5.0f; eng::BodyHandle h=phys.createBody(crate);
    phys.applyImpulse(h, {20,0,0}, {0,0.4f,0});
    for(int i=0;i<60;++i) phys.update(1.0f/60.0f);
    glm::vec3 p; glm::quat q; phys.getRenderTransform(h,p,q);
    CHECK(p.x > 0.3f, "impulse should shove the crate in +x");
    phys.shutdown();
    std::puts("test_impulse_moves_prop OK");
}

// Regression for the arch/portal fall-through bug: an arch cell emits two
// side-block colliders flanking the opening PLUS a floor slab. Reproduce that
// collider layout (mirroring DungeonMap::buildFromLayout's arch branch) and
// confirm a character standing in the doorway rests on the floor instead of
// dropping through the gap between the side blocks.
static void test_fast_projectile_does_not_tunnel_thin_wall() {
    eng::Physics phys; phys.init();
    int contacts = 0;
    phys.setContactCallback([&](const eng::HitEvent&){ contacts++; });
    eng::BodyDesc wall; wall.halfExtents={0.02f,2,2}; wall.position={5,0,0}; // 4cm thin
    wall.layer=eng::BodyLayer::Static; wall.dynamic=false; phys.createBody(wall);
    eng::BodyDesc arrow; arrow.kind=eng::ShapeKind::Capsule; arrow.radius=0.03f; arrow.halfHeight=0.2f;
    arrow.position={0,0,0}; arrow.layer=eng::BodyLayer::Projectile; arrow.continuousCast=true; arrow.mass=0.1f;
    eng::BodyHandle a=phys.createBody(arrow);
    phys.applyImpulse(a,{15,0,0},{0,0,0}); // ~150 m/s
    for(int i=0;i<30;++i) phys.update(1.0f/60.0f);
    glm::vec3 p; glm::quat q; phys.getRenderTransform(a,p,q);
    CHECK(contacts>0, "CCD arrow should register a contact with the thin wall");
    CHECK(p.x < 5.5f, "arrow should not have tunneled far past the wall");
    phys.shutdown();
    std::puts("test_fast_projectile_does_not_tunnel_thin_wall OK");
}

static void test_arch_cell_has_floor() {
    eng::Physics phys; phys.init();
    const float cell = 4.0f, wallH = 3.0f, archHalf = 0.8f;
    const float x0 = 0.0f, z0 = 0.0f;               // arch cell origin
    const float hc = cell * 0.5f;
    auto box = [&](glm::vec3 c, glm::vec3 he) {
        eng::BodyDesc d; d.kind = eng::ShapeKind::Box; d.halfExtents = he;
        d.position = c; d.layer = eng::BodyLayer::Static; d.dynamic = false;
        phys.createBody(d);
    };
    // N-S arch: side blocks span [x0,lo] and [hi,x1]; opening centred on x.
    const float mid = x0 + hc, lo = mid - archHalf, hi = mid + archHalf;
    const float hw0 = (lo - x0) * 0.5f, hw1 = (x0 + cell - hi) * 0.5f;
    box({x0 + hw0, wallH * 0.5f, z0 + hc}, {hw0, wallH * 0.5f, hc});
    box({hi + hw1, wallH * 0.5f, z0 + hc}, {hw1, wallH * 0.5f, hc});
    box({x0 + hc, -0.05f, z0 + hc}, {hc, 0.05f, hc});      // the floor slab (fix)

    eng::CharacterDesc cd; cd.position = {mid, 1.0f, z0 + hc}; // in the opening
    eng::CharacterHandle ch = phys.createCharacter(cd);
    float vy = 0.0f;
    for (int i = 0; i < 180; ++i) {
        vy -= 18.0f / 60.0f;
        if (phys.characterState(ch).grounded() && vy < 0) vy = 0;
        phys.characterSetVelocity(ch, {0, vy, 0});
        phys.characterUpdate(ch, 1.0f / 60.0f);
        phys.update(1.0f / 60.0f);
    }
    eng::CharacterState s = phys.characterState(ch);
    CHECK(s.grounded(), "character in the arch opening should be grounded");
    CHECK(s.position.y > -0.5f, "character must not fall through the arch floor");
    phys.shutdown();
    std::puts("test_arch_cell_has_floor OK");
}

static void test_shapecast_hits_prop_once() {
    eng::Physics phys; phys.init();
    eng::BodyDesc crate; crate.halfExtents={0.4f,0.4f,0.4f}; crate.position={1.5f,0,0};
    crate.layer=eng::BodyLayer::Prop; crate.dynamic=false; eng::BodyHandle c=phys.createBody(crate);
    eng::BodyDesc sweep; sweep.kind=eng::ShapeKind::Sphere; sweep.radius=0.3f;
    std::vector<eng::ShapeHit> hits;
    int n = phys.shapeCast(sweep, {0,0,0}, {2,0,0}, hits, eng::BodyLayer::Prop);
    CHECK(n>=1, "sweep should hit the crate");
    CHECK(hits[0].body==c, "sweep should report the crate body");
    // A static wall on a different layer must be ignored by the Prop mask.
    eng::BodyDesc wall; wall.halfExtents={0.1f,2,2}; wall.position={1.0f,0,0};
    wall.layer=eng::BodyLayer::Static; wall.dynamic=false; phys.createBody(wall);
    std::vector<eng::ShapeHit> hits2;
    int n2 = phys.shapeCast(sweep, {0,0,0}, {2,0,0}, hits2, eng::BodyLayer::Prop);
    CHECK(n2>=1, "prop mask should still hit the crate");
    for (auto& h : hits2) CHECK(h.body==c, "prop-masked sweep must not report the static wall");
    phys.shutdown();
    std::puts("test_shapecast_hits_prop_once OK");
}

static void test_teardown_frees_all_bodies() {
    eng::Physics phys; phys.init();
    std::vector<eng::BodyHandle> hs;
    for (int i=0;i<50;++i){ eng::BodyDesc b; b.position={float(i),2,0}; hs.push_back(phys.createBody(b)); }
    CHECK(phys.bodyCount()==50, "50 bodies live after creation");
    for (auto h: hs) phys.removeBody(h);
    CHECK(phys.bodyCount()==0, "all bodies freed after removeBody");
    phys.shutdown();
    std::puts("test_teardown_frees_all_bodies OK");
}

int main() {
    test_box_falls_and_rests_on_floor();
    test_raycast_hits_static_box();
    test_mesh_body_is_solid();
    test_character_settles_and_is_blocked_by_wall();
    test_character_steps_small_ledge();
    test_impulse_moves_prop();
    test_arch_cell_has_floor();
    test_fast_projectile_does_not_tunnel_thin_wall();
    test_shapecast_hits_prop_once();
    test_teardown_frees_all_bodies();
    return 0;
}
