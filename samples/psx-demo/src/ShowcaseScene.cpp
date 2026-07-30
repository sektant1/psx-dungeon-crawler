#include "ShowcaseScene.h"

#include <eng/LightDesc.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

// The surrounding volume. Its floor is y=0 by construction.
constexpr float kShellSize = 46.0f;

// Top of the dais's upper disc (node at 0.31, 0.18 tall). Anything standing in
// the middle of the circle stands on THIS, not on y=0 -- placing the centre
// piece at 0 buries it a hand's width into the stone.
constexpr float kDaisTop = 0.40f;

// The chest's rest height above the plinth's base, and the swing of its bob.
// plinth top (0.50) + chest half-height (0.44) + the swing + a hand's clearance.
constexpr float kChestBob = 0.22f;
constexpr float kChestHover = 0.50f + 0.44f + kChestBob + 0.05f;

glm::quat yaw(float degrees)
{
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
}

// Polar placement on the dais: angle in degrees, distance from centre.
glm::vec3 polar(float angleDeg, float radius, float y = 0.0f)
{
    const float a = glm::radians(angleDeg);
    return {radius * std::sin(a), y, radius * std::cos(a)};
}

// Linearises an sRGB colour the way the PSX shaders expect. Picking colours in
// sRGB and forgetting this is what makes hand-tuned lights read washed out.
glm::vec3 srgb(float r, float g, float b, float gain = 1.0f)
{
    return glm::vec3(std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f)) *
           gain;
}

} // namespace

bool ShowcaseScene::build(eng::Renderer& renderer, const std::string& assetDir,
                          const ShowcaseOptions& options)
{
    mAssetDir = assetDir;
    mRoot = renderer.createNode(eng::kRootNode, glm::vec3(0.0f), "showcase");

    buildDais(renderer, options);
    // The treasure is the CENTRE piece: it sits on the middle of the dais, the
    // one point the turntable never carries out of frame, and it is the only
    // thing in the scene that levitates. Off on the ring it read as a prop that
    // had come loose from the floor, because two of its three neighbours were
    // also floor-standing objects at the same radius.
    buildTreasure(renderer, assetDir, options, {0.0f, kDaisTop, 0.0f});
    // Two stations flanking it, 120 degrees apart, each readable on its own as
    // the camera comes round: the crystal leads (brightest silhouette), the
    // portal answers it far enough away that the pink and the green never wash
    // into each other.
    buildCrystalShrine(renderer, assetDir, options, polar(0.0f, options.radius));
    buildPortal(renderer, assetDir, options, polar(120.0f, options.radius),
                120.0f);
    if (options.props)
        buildDressing(renderer, assetDir, options);
    buildLighting(renderer, options);

    if (options.particles) {
        // Scene-wide dust, emitted high and falling through everything.
        renderer.spawnParticles("hall_dust",
                                renderer.createNode(mRoot, {0.0f, 6.5f, 0.0f}));
    }
    return true;
}

void ShowcaseScene::buildDais(eng::Renderer& renderer, const ShowcaseOptions& options)
{
    // A shallow stepped dais. Two discs rather than one: the step catches the
    // rim light and gives the floor a readable edge, which a flat plane at this
    // camera angle does not have.
    eng::PrimitiveMeshDesc lower;
    lower.kind = eng::PrimitiveKind::Cylinder;
    lower.radius = options.daisRadius + 0.55f;
    lower.height = 0.22f;
    lower.segments = 64;
    renderer.attachMesh(renderer.createNode(mRoot, {0.0f, 0.11f, 0.0f}),
                        renderer.createPrimitiveMesh(lower), "PSX/ShowcaseStone", false);

    eng::PrimitiveMeshDesc upper;
    upper.kind = eng::PrimitiveKind::Cylinder;
    upper.radius = options.daisRadius;
    upper.height = 0.18f;
    upper.segments = 64;
    renderer.attachMesh(renderer.createNode(mRoot, {0.0f, 0.31f, 0.0f}),
                        renderer.createPrimitiveMesh(upper), "PSX/Floor", false);

    // The surrounding ground: a large inward-facing box, so the scene sits in a
    // dark volume instead of floating against the clear colour. Fog does the
    // rest of the work of hiding its edges.
    eng::PrimitiveMeshDesc shell;
    shell.kind = eng::PrimitiveKind::Box;
    shell.size = glm::vec3(kShellSize);
    shell.inwardFacing = true;
    shell.subdivisions = 20;
    // Centred at HALF its size, so its floor lands on y=0 -- the plane every
    // prop is placed on. Centred anywhere else and the whole set dressing
    // hovers by the difference, which is exactly what it looked like.
    renderer.attachMesh(renderer.createNode(mRoot, {0.0f, kShellSize * 0.5f, 0.0f}),
                        renderer.createPrimitiveMesh(shell), "PSX/Floor", false);
}

void ShowcaseScene::buildCrystalShrine(eng::Renderer& renderer,
                                       const std::string& assetDir,
                                       const ShowcaseOptions& options, glm::vec3 at)
{
    const std::string meshes = assetDir + "/meshes/";
    eng::NodeHandle base = renderer.createNode(mRoot, at);
    // The whole shrine scales as ONE group. The crystal meshes are authored at
    // landscape size -- scaling the spires alone left a boulder-sized base with
    // pebbles on top.
    renderer.setScale(base, glm::vec3(0.42f));

    // The ground slab the spires grow out of, so they are not simply stuck into
    // the floor.
    renderer.attachMesh(renderer.createNode(base, {0.0f, 0.42f, 0.0f}),
                        renderer.loadObj(meshes + "crystal_ground.obj"),
                        "PSX/CrystalGroundPink", false);

    // A slowly turning cluster. Four different spires at varied scale, angle
    // and lean: identical copies at even spacing is what makes a crystal
    // cluster read as a fence rather than as a formation.
    mCrystalSpin = renderer.createNode(base, {0.0f, 0.42f, 0.0f});
    struct Shard {
        const char* mesh;
        float angleDeg, radius, scale, leanDeg;
    };
    const Shard shards[] = {
        // Scales are small: these meshes are authored large, and at 1.0 a single
        // spire is taller than the portal and dwarfs the station it belongs to.
        {"crystal_spire1.obj", 20.0f, 0.0f, 1.25f, 0.0f},   // the tall centre
        {"crystal_spire2.obj", 95.0f, 1.5f, 0.80f, 11.0f},
        {"crystal_spire3.obj", 200.0f, 1.8f, 0.95f, -9.0f},
        {"crystal_spire4.obj", 290.0f, 1.3f, 0.62f, 14.0f},
        {"crystal_spire2.obj", 330.0f, 2.2f, 0.48f, -16.0f}, // low outlier
    };
    for (const Shard& shard : shards) {
        eng::NodeHandle node =
            renderer.createNode(mCrystalSpin, polar(shard.angleDeg, shard.radius));
        renderer.setOrientation(node,
                                yaw(shard.angleDeg) *
                                    glm::angleAxis(glm::radians(shard.leanDeg),
                                                   glm::vec3(1, 0, 0)));
        renderer.setScale(node, glm::vec3(shard.scale));
        // Crystals cast shadows: the shader is translucent-looking, and the
        // shadow is what proves they are solid objects in the scene.
        renderer.attachMesh(node, renderer.loadObj(meshes + shard.mesh),
                            "PSX/CrystalSpirePink", true);
        mCrystalShards.push_back(node);
    }

    if (options.particles) {
        // Parented to the ROOT at the station's position, not to the scaled
        // group: a particle system under a 0.42 scale emits 0.42-sized motes in
        // a 0.42-sized volume, which reads as dust rather than as magic.
        renderer.spawnParticles("crystal_motes",
                                renderer.createNode(mRoot, at + glm::vec3(0.0f, 0.5f, 0.0f)));
        renderer.spawnParticles("crystal_core",
                                renderer.createNode(mRoot, at + glm::vec3(0.0f, 0.35f, 0.0f)));
    }

    // No light shaft here. The cone mesh is modelled standing on its own base
    // (y 0..3) and was being placed 2.2 m up, so it hung in the air beside the
    // station and read as a loose grey slab; dropped onto the floor at a size
    // that reaches the spires it is opaque enough to hide them instead. The
    // station already has the strongest read in the scene -- the pink key light
    // and the mote column -- and does not need a third element competing with
    // the crystals' silhouette.

    eng::LightDesc glow;
    glow.colour = srgb(1.0f, 0.42f, 0.86f, 2.8f); // hot pink
    glow.range = 4.5f;
    mCrystalLight = renderer.attachLight(
        renderer.createNode(mRoot, at + glm::vec3(0.0f, 1.5f, 0.0f)), glow);
}

void ShowcaseScene::buildPortal(eng::Renderer& renderer,
                                const std::string& assetDir,
                                const ShowcaseOptions& options, glm::vec3 at,
                                float facingDeg)
{
    // The portal the GAME builds, piece for piece: same membrane size, same kit
    // surround, same materials, same numbers as createPortalProp() in
    // game/src/SceneFactory.cpp. It is duplicated rather than shared because the
    // sample does not link the game, but it is deliberately not *reinterpreted*
    // -- a station whose whole job is to show the portal off has to be showing
    // the real one. The two beams and a prototype quad that stood here before
    // read as a green sliver in a doorframe and looked nothing like it.
    //
    // Keep in step with createPortalProp; the constants below are its
    // PortalPropStyle defaults.
    const std::string kit = assetDir + "/meshes/kit/";
    constexpr glm::vec2 fieldSize{2.8f, 2.5f}; // the opening, in metres
    constexpr float membraneInset = 0.06f;     // clear of the frame plane
    constexpr float pillarWidth = 0.50f;
    constexpr float headHeight = 0.45f;
    constexpr float frameDepth = 0.42f;
    const float openingHalfWidth = fieldSize.x * 0.5f;
    const float openingHalfHeight = fieldSize.y * 0.5f;
    // The membrane runs the full height of the surround, not just the opening:
    // the Arch's soffit is a curve, so a membrane stopping at the opening's top
    // edge leaves a dark crescent under the arch stones.
    const float membraneHeight = fieldSize.y + headHeight;

    eng::NodeHandle base = renderer.createNode(mRoot, at);
    // Turned to face the centre, so the orbiting camera meets it head-on rather
    // than edge-on (a portal seen edge-on is an invisible plane).
    renderer.setOrientation(base, yaw(facingDeg + 180.0f));
    const eng::NodeHandle arch =
        renderer.createNode(base, {0.0f, openingHalfHeight, 0.0f});

    // Surround: the kit's own Pillar either side and its Arch as the head,
    // baked to metres at load. Kit pieces are authored on a 20-unit grid with
    // their base at Y=0 -- except Arch, which spans Y 4.81..16.81 and so is
    // translated down onto the opening's top edge.
    {
        const eng::NodeHandle frame = renderer.createNode(base);
        const float pillarSide = pillarWidth / 5.65f; // Pillar is 5.65 across
        const glm::mat4 kitToPillar = glm::scale(
            glm::mat4(1.0f), {pillarSide, membraneHeight / 30.2f, pillarSide});
        const eng::MeshHandle pillar =
            renderer.loadObj(kit + "Pillar.obj", &kitToPillar);
        const float jambX = openingHalfWidth + pillarWidth * 0.5f;
        for (const float x : {-jambX, jambX})
            renderer.attachMesh(renderer.createNode(frame, {x, 0.0f, 0.0f}),
                                pillar, "Kit/Dungeon", true);

        const float headScaleX = (fieldSize.x + pillarWidth * 2.0f) / 20.02f;
        const float headScaleY = headHeight / 12.0f;
        const glm::mat4 kitToHead =
            glm::translate(glm::mat4(1.0f), {0.0f, -4.81f * headScaleY, 0.0f}) *
            glm::scale(glm::mat4(1.0f),
                       {headScaleX, headScaleY, frameDepth / 4.0f});
        const eng::MeshHandle head = renderer.loadObj(kit + "Arch.obj", &kitToHead);
        // The head straddles the membrane in depth rather than sitting behind
        // it, so the stone covers the membrane's square top corners while its
        // opening still shows the membrane through.
        renderer.attachMesh(
            renderer.createNode(frame, {0.0f, fieldSize.y,
                                        membraneInset + frameDepth * 0.25f}),
            head, "Kit/Dungeon", true);
    }

    // The membrane: a quad, not a disc -- an inscribed ellipse would leave the
    // corners of the surround showing the rock behind. Offset so its base stays
    // on the floor while its extra height goes up behind the arch head.
    eng::PrimitiveMeshDesc plane;
    plane.kind = eng::PrimitiveKind::Plane;
    plane.size = {fieldSize.x, 1.0f, membraneHeight};
    mPortalNode = renderer.createNode(
        arch, {0.0f, membraneHeight * 0.5f - openingHalfHeight, membraneInset});
    // The plane primitive lies flat and is two-sided; stand it up to fill the
    // archway and it reads from both sides of the threshold.
    renderer.setOrientation(mPortalNode,
                            glm::angleAxis(glm::radians(90.0f),
                                           glm::vec3(1, 0, 0)));
    renderer.attachMesh(mPortalNode, renderer.createPrimitiveMesh(plane),
                        "Game/PortalDown", false);

    if (options.particles) {
        // The game's own portal wisps, an engine preset -- no TOML, and the
        // same effect the descent portal wears in the dungeon.
        renderer.spawnParticles("engine.portal_wisps", arch);
        renderer.spawnParticles("portal_embers",
                                renderer.createNode(base, {0.0f, 0.35f, 0.0f}));
    }

    eng::LightDesc glow;
    // Toned down from 2.2/9.0: with the stations pulled in to 5.2 m the portal
    // reached the middle of the dais and painted the gold chest fel green. The
    // range now stops short of the centre, so each station lights its own
    // ground and the chest keeps its own colour.
    glow.colour = srgb(0.35f, 1.0f, 0.40f, 1.9f); // fel green
    glow.range = 4.0f;
    mPortalLight = renderer.attachLight(
        renderer.createNode(base, {0.0f, 1.6f, 0.8f}), glow);
}

void ShowcaseScene::buildTreasure(eng::Renderer& renderer,
                                  const std::string& assetDir,
                                  const ShowcaseOptions& options, glm::vec3 at)
{
    const std::string props = assetDir + "/meshes/props/";
    eng::NodeHandle base = renderer.createNode(mRoot, at);

    // A low plinth directly under the chest, so the levitation has a reference
    // on the ground. A floating object with nothing beneath it does not read as
    // levitating, it reads as mispositioned -- which is exactly how this chest
    // read out on the ring.
    eng::PrimitiveMeshDesc plinth;
    plinth.kind = eng::PrimitiveKind::BeveledBox;
    plinth.size = {1.5f, 0.5f, 1.5f};
    plinth.bevel = 0.09f;
    renderer.attachMesh(renderer.createNode(base, {0.0f, 0.25f, 0.0f}),
                        renderer.createPrimitiveMesh(plinth), "PSX/ShowcaseStone",
                        true);

    // Hover height measured from the plinth's top (0.50) plus the chest's own
    // half-height (0.08 authored * 5.5 = 0.44) plus the gap the bob swings in,
    // so the chest never dips into the stone at the bottom of its travel.
    mChestBase = renderer.createNode(base, {0.0f, kChestHover, 0.0f});
    mChestSpin = renderer.createNode(mChestBase);
    renderer.setScale(mChestSpin, glm::vec3(5.5f)); // the prop is authored small
    renderer.attachMesh(mChestSpin, renderer.loadObj(props + "prop_chest.obj"),
                        "Game/PropChest", true);

    if (options.particles) {
        // Three layers, because one emitter on a levitating object reads as a
        // smoke leak rather than as enchantment:
        //
        //  - motes orbiting the chest, parented to the bobbing node so they
        //    travel with it;
        //  - a column rising off the plinth, parented to the STATIC base, so
        //    the effect stays anchored to the ground the chest is lifting away
        //    from and the two do not move as one rigid object;
        //  - a tight bright core at the seam of the lid, which is what puts
        //    the eye on the chest itself rather than on the haze.
        //
        // All three loop. The obvious pick for the third layer,
        // engine.hit_sparks, is a burst preset (loop = false): it fires once
        // when the scene is built and the chest is bare for the rest of the
        // run.
        eng::ParticleSpawnOptions orbit;
        orbit.emitterRadius = 0.55f;
        orbit.sizeScale = 1.15f;
        orbit.amountScale = 2.2f;
        orbit.lifetimeScale = 1.3f;
        orbit.colourTint = {1.0f, 0.82f, 0.42f, 1.0f}; // gold
        renderer.spawnParticles("engine.arcane_motes", mChestBase,
                                glm::vec3(0.0f, -0.15f, 0.0f), orbit);

        eng::ParticleSpawnOptions column;
        column.emitterRadius = 0.42f;
        column.lifetimeScale = 1.6f;
        column.speedScale = 0.7f;
        column.sizeScale = 0.9f;
        column.amountScale = 1.8f;
        column.colourTint = {1.0f, 0.74f, 0.34f, 0.8f};
        renderer.spawnParticles("treasure_dust", base,
                                glm::vec3(0.0f, 0.55f, 0.0f), column);

        eng::ParticleSpawnOptions core;
        core.emitterRadius = 0.26f;
        core.sizeScale = 0.8f;
        core.amountScale = 0.8f;
        core.colourTint = {1.0f, 0.92f, 0.55f, 1.0f};
        renderer.spawnParticles("crystal_core", mChestBase,
                                glm::vec3(0.0f, 0.10f, 0.0f), core);
    }

    eng::LightDesc glow;
    // The brightest light in the scene, because the chest is the centrepiece
    // and the two station lights now stop short of it.
    glow.colour = srgb(1.0f, 0.78f, 0.36f, 3.2f); // gold
    glow.range = 5.5f;
    mChestLight = renderer.attachLight(mChestBase, glow);
}

void ShowcaseScene::buildDressing(eng::Renderer& renderer,
                                  const std::string& assetDir,
                                  const ShowcaseOptions& options)
{
    const std::string props = assetDir + "/meshes/props/";
    const auto mesh = [&](const char* file) {
        return renderer.loadObj(props + file);
    };
    const auto place = [&](eng::MeshHandle m, const char* material,
                           glm::vec3 position, float yawDeg,
                           float scale = 1.0f) {
        eng::NodeHandle node = renderer.createNode(mRoot, position);
        if (yawDeg != 0.0f)
            renderer.setOrientation(node, yaw(yawDeg));
        if (scale != 1.0f)
            renderer.setScale(node, glm::vec3(scale));
        renderer.attachMesh(node, m, material, true);
        return node;
    };
    // Two-part props share a node (the OBJs are split per material).
    const auto place2 = [&](eng::MeshHandle a, const char* materialA,
                            eng::MeshHandle b, const char* materialB,
                            glm::vec3 position, float yawDeg) {
        eng::NodeHandle node = place(a, materialA, position, yawDeg);
        renderer.attachMesh(node, b, materialB, true);
        return node;
    };

    eng::MeshHandle barrel0 = mesh("prop_barrel_p0.obj");
    eng::MeshHandle barrel1 = mesh("prop_barrel_p1.obj");
    eng::MeshHandle crate = mesh("prop_crate.obj");
    eng::MeshHandle sack = mesh("prop_jutesack.obj");
    eng::MeshHandle vase0 = mesh("prop_vase_p0.obj");
    eng::MeshHandle vase1 = mesh("prop_vase_p1.obj");

    // Dressing sits in the GAPS between the three stations, at 60/180/300
    // degrees, and outside the dais. Props in front of a station would compete
    // with it; props behind would be lost against it. The gaps are the only
    // place they add depth without costing readability.
    const float ring = options.radius + 1.25f;

    // 60 deg: a working corner -- stacked crates and a leaning barrel. Stacks
    // are rotated a few degrees off each other so the pile looks handled
    // rather than snapped to a grid.
    {
        const glm::vec3 c = polar(60.0f, ring);
        place(crate, "Game/PropMarket", c, 12.0f);
        place(crate, "Game/PropMarket", c + glm::vec3(0.02f, 0.24f, -0.03f),
              -21.0f);
        place(crate, "Game/PropMarket", c + glm::vec3(-0.03f, 0.48f, 0.02f),
              37.0f);
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               polar(52.0f, ring + 0.9f), 24.0f);
        place(sack, "Game/PropJute", polar(68.0f, ring + 1.1f), 105.0f);
    }

    // 180 deg: a market table with goods, facing the centre so its top reads.
    {
        const glm::vec3 c = polar(180.0f, ring);
        eng::NodeHandle table =
            place2(mesh("prop_table_p0.obj"), "Game/PropWood",
                   mesh("prop_table_p1.obj"), "Game/PropMarket",
                   c + glm::vec3(0.0f, 0.88f, 0.0f), 0.0f);
        renderer.attachMesh(renderer.createNode(table, {0.32f, 0.53f, -0.18f}),
                            mesh("prop_bread.obj"), "Game/PropMarketMisc");
        renderer.attachMesh(renderer.createNode(table, {-0.30f, 0.59f, 0.22f}),
                            mesh("prop_pumpkin.obj"), "Game/PropMarketMisc");
        place(mesh("prop_haybale.obj"), "Game/PropHay",
              polar(192.0f, ring + 1.2f), -35.0f);
    }

    // 300 deg: an armoury corner -- a sword stuck in the ground and a shield
    // propped against a barrel. Both weapon meshes are authored huge, hence the
    // scale.
    {
        eng::NodeHandle sword = renderer.createNode(
            mRoot, polar(296.0f, ring) + glm::vec3(0.0f, 1.15f, 0.0f));
        renderer.setScale(sword, glm::vec3(0.06f));
        // Modelled pointing +Y; roll past 180 so it points down, with a lean
        // that reads as "driven into the ground" rather than "dropped".
        renderer.setOrientation(sword,
                                glm::angleAxis(glm::radians(168.0f),
                                               glm::vec3(0, 0, 1)) *
                                    glm::angleAxis(glm::radians(9.0f),
                                                   glm::vec3(1, 0, 0)));
        renderer.attachMesh(sword, mesh("prop_sword.obj"), "Game/PropWeapon",
                            true);

        const glm::vec3 b = polar(306.0f, ring + 0.7f);
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus", b,
               0.0f);
        eng::NodeHandle shield =
            renderer.createNode(mRoot, b + glm::vec3(0.0f, 0.55f, 0.78f));
        renderer.setScale(shield, glm::vec3(0.08f));
        renderer.setOrientation(shield, glm::angleAxis(glm::radians(-20.0f),
                                                       glm::vec3(1, 0, 0)));
        renderer.attachMesh(shield, mesh("prop_shield.obj"), "Game/PropWeapon",
                            true);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               polar(288.0f, ring + 1.3f), 62.0f);
    }
}

void ShowcaseScene::buildLighting(eng::Renderer& renderer,
                                  const ShowcaseOptions& options)
{
    // Blue-hour ambience: dim and cool, so the three coloured station lights
    // are what actually illuminate anything. A bright ambient would flatten
    // them into tinted highlights.
    renderer.setAmbient(srgb(0.42f, 0.48f, 0.72f, 0.22f));
    renderer.setFog(srgb(0.05f, 0.06f, 0.11f), 0.045f);
    renderer.setBackground({0.018f, 0.022f, 0.042f});

    // Lamp posts between the stations, on the same 60/180/300 line as the
    // dressing: warm pools that separate the stations from each other and give
    // the turntable something to sweep past.
    eng::LightDesc lamp;
    lamp.colour = srgb(1.0f, 0.63f, 0.33f, 1.7f);
    lamp.range = 4.5f;
    // The post and the hanging lamp are real geometry. They used to be a bare
    // node with a light on it, which left three warm pools on the floor with
    // nothing casting them.
    const std::string props = mAssetDir + "/meshes/props/";
    const eng::MeshHandle beam = renderer.loadObj(props + "prop_beam.obj");
    const eng::MeshHandle head = renderer.loadObj(props + "prop_lamp.obj");
    for (const float angle : {60.0f, 180.0f, 300.0f}) {
        const glm::vec3 at = polar(angle, options.radius + 2.9f);
        eng::NodeHandle post = renderer.createNode(mRoot, at);
        renderer.setOrientation(post, yaw(angle));
        renderer.attachMesh(post, beam, "Game/PropBauerhaus", true);
        // The beam is 2 m tall; the lamp hangs just over its top.
        eng::NodeHandle lantern =
            renderer.createNode(mRoot, at + glm::vec3(0.0f, 2.1f, 0.0f));
        renderer.attachMesh(lantern, head, "Game/PropLamp", true);
        mLampLights.push_back(renderer.attachLight(lantern, lamp));
    }

    // A pale moon key from above so silhouettes still read where no station
    // light reaches. Weak on purpose: it is a fill, not the light source.
    eng::LightDesc moon;
    moon.type = eng::LightDesc::Type::Directional;
    moon.colour = srgb(0.55f, 0.64f, 0.92f, 0.30f);
    eng::NodeHandle moonNode = renderer.createNode(mRoot, {0.0f, 12.0f, 0.0f});
    renderer.setOrientation(moonNode,
                            glm::angleAxis(glm::radians(-62.0f),
                                           glm::vec3(1, 0, 0)) *
                                yaw(28.0f));
    renderer.attachLight(moonNode, moon);
}

void ShowcaseScene::update(eng::Renderer& renderer, float time)
{
    // Crystal cluster: a slow turn, and a light that breathes on two sines so
    // the pulse never lands on an obvious beat.
    if (mCrystalSpin.valid())
        renderer.setOrientation(mCrystalSpin, yaw(glm::degrees(time * 0.22f)));
    if (mCrystalLight.valid()) {
        const float pulse = 0.88f + 0.09f * std::sin(time * 1.3f) +
                            0.05f * std::sin(time * 3.7f);
        renderer.setLightColour(mCrystalLight,
                                srgb(1.0f, 0.42f, 0.86f, 2.8f) * pulse);
    }

    // Portal: a faster, sharper flicker, because it should read as unstable
    // next to the crystal's steady glow.
    if (mPortalLight.valid()) {
        const float flicker = 0.82f + 0.14f * std::sin(time * 2.6f) +
                              0.08f * std::sin(time * 6.1f + 1.7f);
        renderer.setLightColour(mPortalLight,
                                srgb(0.35f, 1.0f, 0.40f, 1.9f) * flicker);
    }

    // Treasure: the chest bobs and turns, its glow riding the bob so the light
    // and the object agree about where it is.
    if (mChestBase.valid()) {
        const float bob = std::sin(time * 0.85f);
        renderer.setPosition(mChestBase,
                             {0.0f, kChestHover + kChestBob * bob, 0.0f});
        if (mChestSpin.valid()) {
            // Turn, plus a slight tilt that follows the bob: a pure yaw on a
            // levitating object reads mechanical, like a display turntable.
            renderer.setOrientation(
                mChestSpin,
                yaw(glm::degrees(time * 0.55f)) *
                    glm::angleAxis(glm::radians(4.5f * bob), glm::vec3(1, 0, 0)));
        }
        if (mChestLight.valid()) {
            const float pulse = 0.92f + 0.08f * bob;
            renderer.setLightColour(mChestLight,
                                    srgb(1.0f, 0.78f, 0.36f, 3.2f) * pulse);
        }
    }

    // Lamps: a shallow, offset flicker each, so the three do not blink in
    // unison like one switch.
    for (std::size_t i = 0; i < mLampLights.size(); ++i) {
        const float phase = float(i) * 2.1f;
        const float flicker = 0.93f + 0.05f * std::sin(time * 5.3f + phase) +
                              0.03f * std::sin(time * 11.0f + phase);
        renderer.setLightColour(mLampLights[i],
                                srgb(1.0f, 0.63f, 0.33f, 1.7f) * flicker);
    }
}
