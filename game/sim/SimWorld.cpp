#include "SimWorld.h"
#include "GameCollision.h"

#include <eng/assets/AssetRoot.h>

namespace game::sim {

World::World()
{
    mPhysics.init(game::layer::physicsSetup());
    mPhysics.setGravity(0.0f); // combat sim: no falling, hits/DoT only
    // sim_main mounts the game content set before constructing a World; an
    // unresolved table leaves the built-in defaults in place, which is what
    // this already did when the file was missing.
    mVocabulary.load(eng::assets::resolve("magic.toml").string());
    mCombat.init(eng::assets::resolve("weapons.toml").string(), mVocabulary);
}

World::~World()
{
    mPhysics.shutdown();
}

void World::loadWeapons(const std::string& tomlPath)
{
    mCombat.init(tomlPath, mVocabulary);
}

bool World::addCombatant(const std::string& name, float hp, Faction faction,
                         const Resistances& resist)
{
    if (mByName.count(name))
        return false;

    // A minimal dynamic body so the entity has a BodyHandle (knockback target +
    // the director's body<->entity map). It never moves -- physics isn't stepped.
    eng::BodyDesc bd;
    bd.kind = eng::ShapeKind::Box;
    bd.halfExtents = {0.4f, 0.9f, 0.4f};
    bd.dynamic = true;
    bd.mass = 70.0f;
    const eng::BodyHandle body = mPhysics.createBody(bd);

    Health h;
    h.current = h.max = hp;
    const entt::entity e = mCombat.addCombatant(body, h, resist, faction);
    mByName[name] = e;
    mBodies[name] = body;
    return true;
}

bool World::hit(const std::string& target, const std::string& weapon,
                const std::string& source)
{
    auto tb = mBodies.find(target);
    if (tb == mBodies.end())
        return false;
    const entt::entity src = entityOf(source); // entt::null if unknown = neutral
    mCombat.hitBody(mPhysics, tb->second, weapon, src,
                    glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f));
    return true;
}

void World::advance(float dt)
{
    mCombat.tick(dt);
}

entt::entity World::entityOf(const std::string& name) const
{
    auto it = mByName.find(name);
    return it == mByName.end() ? entt::null : it->second;
}

bool World::alive(const std::string& name) const
{
    const entt::entity e = entityOf(name);
    if (e == entt::null) return false;
    const auto& reg = const_cast<CombatDirector&>(mCombat).registry();
    const Health* h = reg.try_get<Health>(e);
    return h && !h->dead();
}

float World::hp(const std::string& name) const
{
    const entt::entity e = entityOf(name);
    if (e == entt::null) return 0.0f;
    const auto& reg = const_cast<CombatDirector&>(mCombat).registry();
    const Health* h = reg.try_get<Health>(e);
    return h ? h->current : 0.0f;
}

int World::activeEffects(const std::string& name) const
{
    const entt::entity e = entityOf(name);
    if (e == entt::null) return 0;
    const auto& reg = const_cast<CombatDirector&>(mCombat).registry();
    const StatusEffects* fx = reg.try_get<StatusEffects>(e);
    return fx ? int(fx->active.size()) : 0;
}

} // namespace game::sim
