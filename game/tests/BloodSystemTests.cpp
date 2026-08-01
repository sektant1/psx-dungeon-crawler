#include "BloodSystem.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "BloodSystemTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float a, float b)
{
    // The file states two-decimal values and the parser widens them through
    // double, so an exact comparison would be asserting on float formatting
    // rather than on the schema.
    return std::fabs(a - b) < 1e-5f;
}

// Fixtures are written at run time rather than committed: they exist to state a
// single malformed or minimal case, and a directory of one-line TOML files next
// to the real asset would only invite someone to load the wrong one.
std::string writeFixture(const char* name, const std::string& contents)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    require(file.good(), "could not open a temporary fixture for writing");
    file << contents;
    file.close();
    return path.string();
}

const game::BloodDecalDef* findDecal(const game::BloodDefinitions& defs,
                                     const std::string& id)
{
    for (const game::BloodDecalDef& decal : defs.decals)
        if (decal.id == id) return &decal;
    return nullptr;
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    // 1. The shipped file. This is the test that fails when someone renames a
    // key or drops a profile the game asks for by name at runtime.
    {
        game::BloodDefinitions defs;
        const std::string path =
            game::test::asset("config/blood.toml");
        require(game::parseBloodDefinitions(path, defs),
                "the shipped blood.toml no longer parses");

        require(defs.profiles.size() == 3,
                "blood.toml no longer defines exactly three profiles");
        const game::BloodProfile* human = nullptr;
        {
            auto it = defs.profiles.find("human");
            require(it != defs.profiles.end(), "profile 'human' is missing");
            human = &it->second;
        }
        require(human->sprayEffect == "blood_spray" &&
                    human->gibEffect == "blood_gibs" &&
                    human->decalProfile == "blood_splat" &&
                    human->poolProfile == "blood_pool" &&
                    human->dripEffect == "blood_drip",
                "profile 'human' no longer names the effects blood.toml states");
        require(near(human->dripHpFraction, 0.35f) &&
                    near(human->amountScale, 1.0f) &&
                    near(human->damageBias, 0.55f),
                "profile 'human' tuning values changed");

        {
            auto it = defs.profiles.find("boss");
            require(it != defs.profiles.end(), "profile 'boss' is missing");
            require(near(it->second.amountScale, 2.4f) &&
                        near(it->second.damageBias, 0.5f) &&
                        near(it->second.dripHpFraction, 0.5f),
                    "profile 'boss' tuning values changed");
        }
        {
            auto it = defs.profiles.find("undead_ichor");
            require(it != defs.profiles.end(),
                    "profile 'undead_ichor' is missing");
            require(it->second.decalProfile == "ichor_splat" &&
                        near(it->second.amountScale, 0.7f) &&
                        near(it->second.damageBias, 0.2f),
                    "profile 'undead_ichor' no longer oozes as authored");
            require(it->second.gibEffect.empty() &&
                        it->second.poolProfile.empty(),
                    "profile 'undead_ichor' gained effects it does not state");
        }

        require(defs.decals.size() == 4,
                "blood.toml no longer defines exactly four decals");
        // Order is load-bearing: profiles name decals, so decals must arrive in
        // file order for registration to precede any reference.
        require(defs.decals[0].id == "blood_splat" &&
                    defs.decals[1].id == "blood_splat_small" &&
                    defs.decals[2].id == "blood_pool" &&
                    defs.decals[3].id == "ichor_splat",
                "the decal ids or their file order changed");

        const game::BloodDecalDef* splat = findDecal(defs, "blood_splat");
        require(splat != nullptr, "decal 'blood_splat' is missing");
        // Every shipped mark must name a texture stem. An empty one resolves to
        // the base decal material, whose texture unit binds nothing at all, and
        // the mark renders as black blocks rather than as the flat tinted quad
        // the field's name suggests -- which is exactly how it shipped.
        for (const game::BloodDecalDef& decal : defs.decals)
            require(!decal.desc.texture.empty(),
                    "a shipped decal has no texture stem; it will render as "
                    "black blocks, not as a tinted quad");
        require(near(splat->desc.sizeMin, 0.18f) &&
                    near(splat->desc.sizeMax, 0.34f),
                "decal 'blood_splat' size range changed");
        require(near(splat->desc.colour.x, 0.40f) &&
                    near(splat->desc.colour.y, 0.03f) &&
                    near(splat->desc.colour.z, 0.025f) &&
                    near(splat->desc.colour.w, 0.92f),
                "decal 'blood_splat' colour changed");
        require(near(splat->desc.colourJitter.x, 0.06f) &&
                    near(splat->desc.colourJitter.y, 0.015f) &&
                    near(splat->desc.colourJitter.z, 0.015f),
                "decal 'blood_splat' colour jitter changed");
        require(near(splat->desc.alphaJitter, 0.12f),
                "decal 'blood_splat' alpha jitter changed");
        require(near(splat->desc.lifetime, 0.0f) &&
                    near(splat->desc.fadeTime, 1.5f),
                "decal 'blood_splat' is no longer permanent-until-evicted");
        require(near(splat->desc.normalOffset, 0.012f),
                "decal 'blood_splat' normal offset changed");
        require(splat->desc.blend == eng::ParticleBlend::Alpha,
                "decal 'blood_splat' is no longer alpha blended");

        const game::BloodDecalDef* small =
            findDecal(defs, "blood_splat_small");
        require(small != nullptr, "decal 'blood_splat_small' is missing");
        require(near(small->desc.sizeMin, 0.07f) &&
                    near(small->desc.sizeMax, 0.13f) &&
                    near(small->desc.lifetime, 45.0f) &&
                    near(small->desc.fadeTime, 6.0f),
                "decal 'blood_splat_small' values changed");

        const game::BloodDecalDef* pool = findDecal(defs, "blood_pool");
        require(pool != nullptr, "decal 'blood_pool' is missing");
        require(pool->desc.pool, "decal 'blood_pool' stopped pooling");
        require(near(pool->desc.growthRate, 0.10f) &&
                    near(pool->desc.maxSize, 1.05f) &&
                    near(pool->desc.mergeRadius, 0.55f),
                "decal 'blood_pool' growth or merge tuning changed");

        const game::BloodDecalDef* ichor = findDecal(defs, "ichor_splat");
        require(ichor != nullptr, "decal 'ichor_splat' is missing");
        require(ichor->desc.blend == eng::ParticleBlend::Additive,
                "decal 'ichor_splat' no longer glows additively");
        require(near(ichor->desc.colour.y, 0.45f),
                "decal 'ichor_splat' is no longer green");
        require(near(ichor->desc.lifetime, 30.0f) &&
                    near(ichor->desc.fadeTime, 5.0f),
                "decal 'ichor_splat' lifetime changed");
    }

    // 2. Defaults. A minimal block must be usable, because the documented way to
    // add a creature is to state only what differs from the norm.
    {
        const std::string path = writeFixture("blood_defaults_test.toml",
                                              "[[decal]]\n"
                                              "id = \"bare\"\n"
                                              "\n"
                                              "[[profile]]\n"
                                              "id = \"bare\"\n");
        game::BloodDefinitions defs;
        require(game::parseBloodDefinitions(path, defs),
                "a minimal blood file failed to parse");

        const game::BloodProfile& p = defs.profiles.at("bare");
        require(p.sprayEffect.empty() && p.gibEffect.empty() &&
                    p.mistEffect.empty() && p.decalProfile.empty() &&
                    p.poolProfile.empty() && p.dripEffect.empty(),
                "an omitted effect name did not default to empty");
        require(near(p.dripHpFraction, 0.35f),
                "drip_hp_fraction default changed");
        require(near(p.amountScale, 1.0f), "amount_scale default changed");
        require(near(p.damageBias, 0.55f), "damage_bias default changed");

        const eng::DecalProfileDesc reference;
        const eng::DecalProfileDesc& d = defs.decals.at(0).desc;
        require(near(d.sizeMin, reference.sizeMin) &&
                    near(d.sizeMax, reference.sizeMax) &&
                    near(d.lifetime, reference.lifetime) &&
                    near(d.fadeTime, reference.fadeTime) &&
                    near(d.normalOffset, reference.normalOffset) &&
                    near(d.maxSize, reference.maxSize) &&
                    d.pool == reference.pool &&
                    d.randomRotation == reference.randomRotation,
                "an omitted decal key did not fall back to the struct default");
        require(d.blend == eng::ParticleBlend::Alpha,
                "an omitted blend did not default to alpha");
    }

    // 3. Clamping. Out-of-range tuning must be corrected at the boundary rather
    // than reaching the spray maths, where a negative amount silently kills the
    // effect and a bias above one denormalises the direction blend.
    {
        const std::string path = writeFixture("blood_clamp_test.toml",
                                              "[[profile]]\n"
                                              "id = \"high\"\n"
                                              "damage_bias = 4.0\n"
                                              "drip_hp_fraction = 9.0\n"
                                              "amount_scale = -2.0\n"
                                              "\n"
                                              "[[profile]]\n"
                                              "id = \"low\"\n"
                                              "damage_bias = -3.0\n"
                                              "drip_hp_fraction = -1.0\n");
        game::BloodDefinitions defs;
        require(game::parseBloodDefinitions(path, defs),
                "the clamping fixture failed to parse");

        const game::BloodProfile& high = defs.profiles.at("high");
        require(near(high.damageBias, 1.0f),
                "damage_bias above one was not clamped");
        require(near(high.dripHpFraction, 1.0f),
                "drip_hp_fraction above one was not clamped");
        require(near(high.amountScale, 0.0f),
                "a negative amount_scale was not clamped");

        const game::BloodProfile& low = defs.profiles.at("low");
        require(near(low.damageBias, 0.0f),
                "a negative damage_bias was not clamped");
        require(near(low.dripHpFraction, 0.0f),
                "a negative drip_hp_fraction was not clamped");
    }

    // 4. Malformed input. One bad block must cost only that block: blood is
    // cosmetic, and a level should never fail to bleed because of a typo.
    {
        game::BloodDefinitions missing;
        const std::string absent =
            (std::filesystem::temp_directory_path() / "blood_does_not_exist.toml")
                .string();
        std::filesystem::remove(absent);
        require(!game::parseBloodDefinitions(absent, missing),
                "a missing file was reported as a successful parse");
        require(missing.profiles.empty() && missing.decals.empty(),
                "a missing file produced definitions out of nowhere");

        const std::string path = writeFixture("blood_malformed_test.toml",
                                              "[[decal]]\n"
                                              "size_min = 0.5\n"
                                              "\n"
                                              "[[decal]]\n"
                                              "id = \"weird\"\n"
                                              "blend = \"screen\"\n"
                                              "\n"
                                              "[[profile]]\n"
                                              "amount_scale = 3.0\n"
                                              "\n"
                                              "[[profile]]\n"
                                              "id = \"good\"\n"
                                              "amount_scale = 1.5\n");
        game::BloodDefinitions defs;
        require(game::parseBloodDefinitions(path, defs),
                "bad blocks aborted the whole parse");
        require(defs.decals.size() == 1 && defs.decals[0].id == "weird",
                "an id-less decal was not skipped");
        require(defs.decals[0].desc.blend == eng::ParticleBlend::Alpha,
                "an unknown blend did not fall back to alpha");
        require(defs.profiles.size() == 1 &&
                    defs.profiles.count("good") == 1,
                "an id-less profile was not skipped");
        require(near(defs.profiles.at("good").amountScale, 1.5f),
                "the surviving profile lost its values");
    }

    // 5. Severity. The only ordering a call site can rely on is that a heavier
    // blow bleeds more; the absolute numbers are free to be retuned.
    {
        require(game::bloodSeverityScale(game::BloodSeverity::Heavy) >
                    game::bloodSeverityScale(game::BloodSeverity::Normal),
                "Heavy does not bleed more than Normal");
        require(game::bloodSeverityScale(game::BloodSeverity::Normal) >
                    game::bloodSeverityScale(game::BloodSeverity::Light),
                "Normal does not bleed more than Light");
        require(game::bloodSeverityScale(game::BloodSeverity::Light) > 0.0f,
                "Light produces no blood at all");
    }

    // 6. Severity from a damage event. This is the decision the call sites make
    // on every hit, and it is a pure function precisely so it can be asserted
    // here rather than eyeballed in a fight: a scratch must not throw gibs, and
    // a killing blow must, whatever fraction of the health bar it took.
    {
        using game::BloodSeverity;
        require(game::bloodSeverityFor(2.0f, 100.0f, false) ==
                    BloodSeverity::Light,
                "a scratch is not Light");
        require(game::bloodSeverityFor(15.0f, 100.0f, false) ==
                    BloodSeverity::Normal,
                "an ordinary hit is not Normal");
        require(game::bloodSeverityFor(45.0f, 100.0f, false) ==
                    BloodSeverity::Heavy,
                "a maiming hit is not Heavy");
        require(game::bloodSeverityFor(1.0f, 100.0f, true) ==
                    BloodSeverity::Heavy,
                "a killing blow is not Heavy");
        // Fractions, not absolutes: the same 15 damage is a scratch on a boss
        // and a maiming on a rat, and a missing/zero max must not divide by it.
        require(game::bloodSeverityFor(15.0f, 500.0f, false) ==
                    BloodSeverity::Light,
                "damage is being read as an absolute rather than a fraction");
        require(game::bloodSeverityFor(15.0f, 0.0f, false) ==
                    BloodSeverity::Normal,
                "an unknown maximum health should fall back to Normal");
    }

    std::cout << "BloodSystemTests OK\n";
}
