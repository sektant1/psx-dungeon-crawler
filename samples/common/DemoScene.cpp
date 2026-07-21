#include "DemoScene.h"

#include <eng/Log.h>
#include <eng/Math.h>
#include <eng/Renderer.h>

#include <glm/gtc/matrix_transform.hpp> // glm::scale
#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cmath>

namespace {

// Godot linear-space shading: sRGB editor colours linearised, energy
// multiplied after.
float lin(float srgb) { return std::pow(srgb, 2.2f); }

// --- strict TOML accessors: scene files are authored, missing keys fatal ---

const toml::table& tableAt(const toml::table& t, const char* key)
{
    const toml::table* sub = t[key].as_table();
    if (!sub)
        eng::log::fatal("DemoScene: missing table [%s]", key);
    return *sub;
}

float numAt(const toml::table& t, const char* key)
{
    auto v = t[key].value<double>();
    if (!v)
        eng::log::fatal("DemoScene: missing number '%s'", key);
    return static_cast<float>(*v);
}

std::string strAt(const toml::table& t, const char* key)
{
    auto v = t[key].value<std::string>();
    if (!v)
        eng::log::fatal("DemoScene: missing string '%s'", key);
    return *v;
}

std::vector<float> floatsAt(const toml::table& t, const char* key, size_t n)
{
    const toml::array* arr = t[key].as_array();
    if (!arr || arr->size() != n)
        eng::log::fatal("DemoScene: '%s' must be an array of %zu numbers",
                        key, n);
    std::vector<float> out;
    out.reserve(n);
    for (const auto& e : *arr)
        out.push_back(static_cast<float>(e.value_or(0.0)));
    return out;
}

glm::vec3 vec3At(const toml::table& t, const char* key)
{
    const std::vector<float> v = floatsAt(t, key, 3);
    return {v[0], v[1], v[2]};
}

glm::vec3 linVec3At(const toml::table& t, const char* key)
{
    const glm::vec3 c = vec3At(t, key);
    return {lin(c.x), lin(c.y), lin(c.z)};
}

glm::quat quatFromRows(const std::vector<float>& r)
{
    return eng::quatFromBasisRows(r[0], r[1], r[2], r[3], r[4], r[5],
                                  r[6], r[7], r[8]);
}

// Basis given as rows a..i (tscn order), zero translation.
glm::mat4 rowsBake(const std::vector<float>& r)
{
    return glm::mat4(r[0], r[3], r[6], 0, r[1], r[4], r[7], 0,
                     r[2], r[5], r[8], 0, 0, 0, 0, 1);
}

} // namespace

bool DemoScene::load(eng::Renderer& r, const std::string& sceneToml,
                     const std::string& meshDir, eng::NodeHandle sunParent,
                     const Options& opts)
{
    toml::parse_result result = toml::parse_file(sceneToml);
    if (!result) {
        eng::log::error("DemoScene: failed to parse %s: %s", sceneToml.c_str(),
                        std::string(result.error().description()).c_str());
        return false;
    }
    const toml::table& root = result.table();

    // ------------------------------------------------------------- env ---
    {
        const toml::table& env = tableAt(root, "env");
        r.setAmbient(linVec3At(env, "ambient_srgb") *
                     numAt(env, "ambient_energy"));
        r.setFog(linVec3At(env, "fog_srgb"), numAt(env, "fog_density"));
        r.setBackground(vec3At(env, "background_srgb"));
        r.setDitherEnabled(env["dither"].value_or(true));
    }

    // ------------------------------------------------------------- sun ---
    {
        const toml::table& sun = tableAt(root, "sun");
        mSun = r.createNode(sunParent, vec3At(sun, "position"));
        r.setOrientation(mSun, quatFromRows(floatsAt(sun, "basis_rows", 9)));
        eng::LightDesc light;
        light.type = eng::LightDesc::Type::Directional;
        light.colour = vec3At(sun, "colour");
        mSunLight = r.attachLight(mSun, light);
    }

    // ----------------------------------------------------------- omnis ---
    {
        const toml::table& omni = tableAt(root, "omni");
        eng::LightDesc light;
        light.colour = linVec3At(omni, "colour_srgb") * numAt(omni, "energy");
        light.range = numAt(omni, "range");
        const float y = numAt(omni, "y");
        const toml::array* xs = omni["x_positions"].as_array();
        if (!xs)
            eng::log::fatal("DemoScene: missing array 'omni.x_positions'");
        for (const auto& x : *xs) {
            eng::NodeHandle n = r.createNode(
                eng::kRootNode, {float(x.value_or(0.0)), y, 0.0f});
            r.attachLight(n, light);
            mOmnis.push_back(n);
        }
    }

    // ------------------------------------------------------ light shaft ---
    {
        const toml::table& shaft = tableAt(root, "light_shaft");
        eng::MeshHandle mesh = r.loadObj(meshDir + strAt(shaft, "mesh"));
        r.attachMesh(r.createNode(eng::kRootNode, vec3At(shaft, "position")),
                     mesh, strAt(shaft, "material"));
    }

    // --------------------------------------- animated boxes + sparkles ---
    if (opts.boxes) {
        const toml::table& boxes = tableAt(root, "boxes");
        const float y = numAt(boxes, "y");
        const glm::vec3 scale{numAt(boxes, "scale")};
        struct Box {
            const char *meshKey, *matKey, *xKey;
            bool sparkles, reverse;
        };
        const Box defs[2] = {
            {"metal_mesh", "metal_material", "metal_x", true, false},
            {"lit_mesh", "lit_material", "lit_x", false, true},
        };
        for (const Box& d : defs) {
            eng::MeshHandle mesh = r.loadObj(meshDir + strAt(boxes, d.meshKey));
            eng::NodeHandle base =
                r.createNode(eng::kRootNode, {numAt(boxes, d.xKey), y, 0.0f});
            r.setScale(base, scale);
            eng::NodeHandle anim = r.createNode(base);
            r.attachMesh(anim, mesh, strAt(boxes, d.matKey));
            if (d.sparkles)
                r.attachParticles(anim, strAt(boxes, "sparkles"));
            mSinPans.push_back({anim, d.reverse});
        }
    }

    // ---------------------------------------------------- blob shadows ---
    if (opts.blobShadows) {
        const toml::table& shadows = tableAt(root, "shadows");
        eng::MeshHandle plane = r.createPlane(numAt(shadows, "plane_size"));
        const float y = numAt(shadows, "y");
        const std::string material = strAt(shadows, "material");
        for (bool reverse : {false, true}) {
            eng::NodeHandle base = r.createNode(
                eng::kRootNode, {reverse ? 1.0f : -1.0f, 0.0f, 0.0f});
            r.attachMesh(r.createNode(base, {0.0f, y, 0.0f}), plane, material);
            mShadowScales.push_back({base, reverse});
        }
    }

    // -------------------------------------------------------- crystals ---
    if (opts.crystals) {
        const toml::table& crystals = tableAt(root, "crystals");
        eng::MeshHandle ground =
            r.loadObj(meshDir + strAt(crystals, "ground_mesh"));
        const std::string groundMat = strAt(crystals, "ground_material");
        const std::string spireMat = strAt(crystals, "spire_material");
        const glm::vec3 groundOffset = vec3At(crystals, "ground_offset");
        const glm::vec3 groundScale{numAt(crystals, "ground_scale")};

        std::vector<eng::MeshHandle> spires;
        const toml::array* spireDefs = crystals["spire"].as_array();
        if (!spireDefs)
            eng::log::fatal("DemoScene: missing [[crystals.spire]] entries");
        for (const auto& e : *spireDefs) {
            const toml::table& spire = *e.as_table();
            glm::mat4 bake;
            if (spire.contains("rows")) {
                bake = rowsBake(floatsAt(spire, "rows", 9));
            } else {
                const std::vector<float> q = floatsAt(spire, "quat", 4);
                bake = glm::mat4_cast(glm::quat(q[0], q[1], q[2], q[3])) *
                       glm::scale(glm::mat4(1.0f), vec3At(spire, "scale"));
            }
            spires.push_back(r.loadObj(meshDir + strAt(spire, "mesh"), &bake));
        }

        const toml::array* instances = crystals["instance"].as_array();
        if (!instances)
            eng::log::fatal("DemoScene: missing [[crystals.instance]] entries");
        for (const auto& e : *instances) {
            const toml::table& inst = *e.as_table();
            eng::NodeHandle node =
                r.createNode(eng::kRootNode, vec3At(inst, "origin"));
            r.setOrientation(node,
                             quatFromRows(floatsAt(inst, "basis_rows", 9)));
            eng::NodeHandle groundNode = r.createNode(node, groundOffset);
            r.setScale(groundNode, groundScale);
            r.attachMesh(groundNode, ground, groundMat);
            for (const eng::MeshHandle& spire : spires)
                r.attachMesh(groundNode, spire, spireMat);
        }
    }

    update(r, 0.0f); // settle animated nodes before the first frame
    return true;
}

void DemoScene::update(eng::Renderer& r, float t) const
{
    // world/spatial_sin_pan.gd: offset = T(0,sin(t)*dir,0) * Euler-YXZ(t,t,t)
    for (const Anim& p : mSinPans) {
        const float dir = p.reverse ? -1.0f : 1.0f;
        r.setPosition(p.node, {0.0f, std::sin(t) * dir, 0.0f});
        r.setOrientation(p.node, glm::angleAxis(t, glm::vec3(0, 1, 0)) *
                                     glm::angleAxis(t, glm::vec3(1, 0, 0)) *
                                     glm::angleAxis(t, glm::vec3(0, 0, 1)));
    }
    // world/shadow/shadow.gd: scale = 0.775 + sin(t) * 0.125 * dir
    for (const Anim& s : mShadowScales) {
        const float dir = s.reverse ? 1.0f : -1.0f;
        const float scale = 0.775f + std::sin(t) * 0.125f * dir;
        r.setScale(s.node, {scale, scale, scale});
    }
}
