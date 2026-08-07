// Headless tests for the decal budget, the merge grid and the profile
// registry. DecalSystem needs no renderer to age, merge, evict and count --
// only the instance-buffer rebuild wants one -- so everything asserted below
// runs with no graphics context at all.
//
// Not covered, because it is unreachable without a live device: material
// resolution per texture stem, batch capacity and visibility, and
// back-to-front sorting. Those live inside the batch rebuild, which is never
// reached headlessly.
#include <eng/particles/DecalSystem.h>

#include <eng/particles/ParticleCollider.h>   // DecalRequest

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "DecalSystemTests: " << message << '\n';
        std::exit(1);
    }
}

std::string num(long long v) { return std::to_string(v); }

std::string num(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return std::string(buf);
}

std::string count(const eng::DecalSystem& decals)
{
    return num((long long)decals.count());
}

eng::DecalRequest hit(const std::string& profile, glm::vec3 pos,
                      glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f))
{
    eng::DecalRequest r;
    r.profile = profile;
    r.position = pos;
    r.normal = normal;
    return r;
}

eng::DecalProfileDesc splatProfile()
{
    eng::DecalProfileDesc desc;
    desc.texture = "";          // untextured: no material lookup is needed
    desc.sizeMin = 0.2f;
    desc.sizeMax = 0.3f;
    desc.lifetime = 0.0f;       // permanent until the budget evicts it
    desc.pool = false;
    return desc;
}

eng::DecalProfileDesc poolProfile()
{
    eng::DecalProfileDesc desc = splatProfile();
    desc.pool = true;
    desc.mergeRadius = 0.5f;
    desc.growthRate = 1.0f;
    desc.maxSize = 1.0f;
    return desc;
}

// --- profile registry ------------------------------------------------------

void testProfileRegistry()
{
    using namespace eng;
    DecalSystem decals;

    require(decals.profile("nothing") == nullptr,
            "an unregistered profile id returned a description");

    DecalProfileDesc desc = splatProfile();
    desc.sizeMin = 0.11f;
    decals.registerProfile("splat", desc);
    const DecalProfileDesc* got = decals.profile("splat");
    require(got != nullptr, "a registered profile could not be looked up");
    require(got->sizeMin == 0.11f,
            "registered profile sizeMin was " + num(double(got->sizeMin)) +
                ", expected 0.11");

    // Re-registration is the hot-reload path: it must replace, not duplicate.
    desc.sizeMin = 0.77f;
    decals.registerProfile("splat", desc);
    got = decals.profile("splat");
    require(got != nullptr, "re-registering a profile dropped it");
    require(got->sizeMin == 0.77f,
            "re-registering did not replace the profile: sizeMin is " +
                num(double(got->sizeMin)) + ", expected 0.77");

    require(!decals.spawn(hit("unknown_profile", glm::vec3(0.0f))),
            "spawn accepted an unknown profile id");
    require(decals.count() == 0,
            "an unknown profile still produced " + count(decals) + " decals");

    require(!decals.spawn(hit("splat", glm::vec3(0.0f), glm::vec3(0.0f))),
            "spawn accepted a zero-length hit normal, which has no plane");
    require(decals.count() == 0,
            "a degenerate normal still produced " + count(decals) + " decals");

    require(decals.spawn(hit("splat", glm::vec3(1.0f, 2.0f, 3.0f))),
            "spawn rejected a valid request");
    require(decals.count() == 1,
            "one valid spawn produced " + count(decals) + " decals, expected 1");

    decals.clear();
    require(decals.count() == 0,
            "clear() left " + count(decals) + " decals behind");
    decals.setVisible(false);   // must be safe with no batches attached
    decals.setVisible(true);
}

// --- budget and LRU eviction ----------------------------------------------

void testBudgetAndEviction()
{
    using namespace eng;
    DecalSystem decals;
    decals.registerProfile("splat", splatProfile());

    require(decals.budget() > 0, "the default budget was zero");
    decals.setBudget(0);
    require(decals.budget() == 1,
            "setBudget(0) resolved to " + num((long long)decals.budget()) +
                "; there is deliberately no unlimited setting, so it must "
                "clamp to 1");

    decals.setBudget(4);
    for (int i = 0; i < 20; ++i) {
        // Spread far apart so nothing merges and every hit is a new decal.
        const glm::vec3 p(float(i) * 5.0f, 0.0f, 0.0f);
        require(decals.spawn(hit("splat", p)),
                "spawn " + num((long long)i) + " failed at " +
                    num(double(p.x)) + " on the x axis");
        require(decals.count() <= 4,
                "the budget was exceeded after spawn " + num((long long)i) +
                    ": " + count(decals) + " live, budget 4");
    }
    require(decals.count() == 4,
            "20 spawns under a budget of 4 left " + count(decals) +
                " decals, expected exactly 4");

    // Lowering the budget evicts immediately rather than waiting for a spawn.
    decals.setBudget(2);
    require(decals.count() == 2,
            "lowering the budget to 2 left " + count(decals) +
                " decals, expected 2");

    // Eviction must drop the *oldest*. Ages are not observable, so the test
    // uses lifetime as a proxy: three mortal decals are spawned first, then
    // three permanent ones under a budget of three. If the newest survived,
    // ageing past the mortal lifetime removes nothing.
    DecalSystem lru;
    DecalProfileDesc mortal = splatProfile();
    mortal.lifetime = 1.0f;
    mortal.fadeTime = 0.1f;
    lru.registerProfile("mortal", mortal);
    lru.registerProfile("permanent", splatProfile());
    lru.setBudget(3);
    for (int i = 0; i < 3; ++i)
        lru.spawn(hit("mortal", glm::vec3(float(i) * 5.0f, 0.0f, 0.0f)));
    require(lru.count() == 3,
            "LRU fixture expected 3 mortal decals, got " + count(lru));
    for (int i = 0; i < 3; ++i)
        lru.spawn(hit("permanent", glm::vec3(float(i) * 5.0f, 0.0f, 20.0f)));
    require(lru.count() == 3,
            "LRU fixture exceeded its budget: " + count(lru) + " of 3");
    lru.update(5.0f);
    require(lru.count() == 3,
            "eviction kept the oldest decals instead of the newest: after "
            "ageing 5s past the mortal 1s lifetime, " + count(lru) +
                " of 3 permanent decals remain (expected 3)");
}

// --- lifetime and ageing ---------------------------------------------------

void testLifetime()
{
    using namespace eng;
    DecalSystem decals;
    DecalProfileDesc mortal = splatProfile();
    mortal.lifetime = 2.0f;
    mortal.fadeTime = 0.5f;
    decals.registerProfile("mortal", mortal);
    decals.registerProfile("permanent", splatProfile());

    decals.spawn(hit("mortal", glm::vec3(0.0f, 0.0f, 0.0f)));
    decals.spawn(hit("permanent", glm::vec3(10.0f, 0.0f, 0.0f)));
    require(decals.count() == 2,
            "lifetime fixture expected 2 decals, got " + count(decals));

    // A negative step must not age anything -- it is clamped, not applied.
    decals.update(-100.0f);
    require(decals.count() == 2,
            "a negative dt aged decals to death: " + count(decals) +
                " left of 2");

    decals.update(1.9f);
    require(decals.count() == 2,
            "a decal died before its 2s lifetime: " + count(decals) +
                " left of 2 at t=1.9");

    decals.update(0.2f);
    require(decals.count() == 1,
            "the mortal decal did not retire at its 2s lifetime: " +
                count(decals) + " left, expected 1 (the permanent one)");

    decals.update(1000.0f);
    require(decals.count() == 1,
            "a lifetime=0 decal expired by time: " + count(decals) +
                " left, expected 1 (permanent until evicted)");
}

// --- merging ---------------------------------------------------------------

void testMerging()
{
    using namespace eng;
    {   // Repeated nearby hits on the same plane feed one pool.
        DecalSystem decals;
        decals.registerProfile("pool", poolProfile());
        for (int i = 0; i < 40; ++i) {
            const glm::vec3 p(0.05f * float(i % 4), 0.0f, 0.05f * float(i % 3));
            require(decals.spawn(hit("pool", p)),
                    "a merging spawn reported failure at hit " +
                        num((long long)i));
        }
        require(decals.count() == 1,
                "40 hits within the merge radius produced " + count(decals) +
                    " decals, expected 1 pool");
    }

    {   // Merging is off unless the profile asks for it.
        DecalSystem decals;
        decals.registerProfile("splat", splatProfile());
        for (int i = 0; i < 5; ++i)
            decals.spawn(hit("splat", glm::vec3(0.01f * float(i), 0.0f, 0.0f)));
        require(decals.count() == 5,
                "a non-pooling profile merged: " + count(decals) +
                    " decals from 5 coincident hits, expected 5");
    }

    {   // mergeRadius = 0 disables merging even for a pool.
        DecalSystem decals;
        DecalProfileDesc noMerge = poolProfile();
        noMerge.mergeRadius = 0.0f;
        decals.registerProfile("pool", noMerge);
        for (int i = 0; i < 5; ++i)
            decals.spawn(hit("pool", glm::vec3(0.01f * float(i), 0.0f, 0.0f)));
        require(decals.count() == 5,
                "mergeRadius=0 still merged: " + count(decals) +
                    " decals from 5 coincident hits, expected 5");
    }

    {   // Beyond the radius, on the same plane: two separate pools.
        DecalSystem decals;
        decals.registerProfile("pool", poolProfile());
        decals.spawn(hit("pool", glm::vec3(0.0f, 0.0f, 0.0f)));
        decals.spawn(hit("pool", glm::vec3(0.9f, 0.0f, 0.0f)));
        require(decals.count() == 2,
                "hits 0.9 apart merged under a 0.5 merge radius: " +
                    count(decals) + " decals, expected 2");
    }

    {   // Same point, perpendicular normals: a floor pool must not swallow a
        // hit on the wall it meets.
        DecalSystem decals;
        decals.registerProfile("pool", poolProfile());
        decals.spawn(hit("pool", glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        decals.spawn(hit("pool", glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
        require(decals.count() == 2,
                "a floor decal merged with a wall decal at the same point: " +
                    count(decals) + " decals, expected 2");
    }

    {   // Parallel normals but different planes: a pool on the floor must not
        // swallow a hit on the ceiling-facing surface above it.
        DecalSystem decals;
        decals.registerProfile("pool", poolProfile());
        decals.spawn(hit("pool", glm::vec3(0.0f, 0.0f, 0.0f)));
        decals.spawn(hit("pool", glm::vec3(0.0f, 0.4f, 0.0f)));
        require(decals.count() == 2,
                "two coplanar-normal hits 0.4 apart along the normal merged: " +
                    count(decals) + " decals, expected 2");
    }

    {   // Different profiles never merge into each other.
        DecalSystem decals;
        decals.registerProfile("blood", poolProfile());
        decals.registerProfile("oil", poolProfile());
        decals.spawn(hit("blood", glm::vec3(0.0f)));
        decals.spawn(hit("oil", glm::vec3(0.05f, 0.0f, 0.0f)));
        require(decals.count() == 2,
                "two different profiles merged: " + count(decals) +
                    " decals, expected 2");
    }

    {   // A fed pool is fresh again, otherwise a puddle under continuous
        // bleeding fades out while it is still being fed.
        DecalSystem decals;
        DecalProfileDesc mortalPool = poolProfile();
        mortalPool.lifetime = 1.0f;
        mortalPool.fadeTime = 0.2f;
        decals.registerProfile("pool", mortalPool);
        decals.spawn(hit("pool", glm::vec3(0.0f)));
        decals.update(0.9f);
        require(decals.count() == 1,
                "the pool died before it was fed: " + count(decals));
        decals.spawn(hit("pool", glm::vec3(0.05f, 0.0f, 0.0f)));
        require(decals.count() == 1,
                "feeding the pool added a decal: " + count(decals) +
                    ", expected 1");
        decals.update(0.5f);
        require(decals.count() == 1,
                "feeding did not reset the pool's age: it died at a total "
                "elapsed 1.4s despite a 1.0s lifetime restarted at 0.9s");
        decals.update(1.5f);
        require(decals.count() == 0,
                "a fed pool never expires: " + count(decals) +
                    " left 1.5s after the last feed with a 1.0s lifetime");
    }

    {   // Merging must keep the count inside the budget too.
        DecalSystem decals;
        decals.registerProfile("pool", poolProfile());
        decals.setBudget(3);
        for (int i = 0; i < 50; ++i)
            decals.spawn(hit("pool", glm::vec3(float(i) * 5.0f, 0.0f, 0.0f)));
        require(decals.count() == 3,
                "pooled spawns spread past the merge radius blew the budget: " +
                    count(decals) + " live, budget 3");
    }
}

// --- headless lifecycle ----------------------------------------------------

void testHeadlessLifecycle()
{
    using namespace eng;
    DecalSystem decals;
    decals.registerProfile("splat", splatProfile());
    decals.spawn(hit("splat", glm::vec3(0.0f)));
    decals.update(0.016f);
    decals.update(0.016f, glm::vec3(0.0f, 1.6f, -3.0f));
    require(decals.count() == 1,
            "a decal was lost while running with no scene manager: " +
                count(decals));
    decals.shutdown();
    require(decals.count() == 0,
            "shutdown() left " + count(decals) + " decals behind");
    // Profiles survive shutdown; only the decals and batches are dropped.
    require(decals.profile("splat") != nullptr,
            "shutdown() dropped the profile registry");
}

} // namespace

int main()
{
    testProfileRegistry();
    testBudgetAndEviction();
    testLifetime();
    testMerging();
    testHeadlessLifecycle();
    std::cout << "DecalSystemTests OK\n";
}
