#include "render/AssimpLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "AssimpModelImportTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool near(glm::vec3 lhs, glm::vec3 rhs, float epsilon = 0.0001f)
{
    return near(lhs.x, rhs.x, epsilon) && near(lhs.y, rhs.y, epsilon) &&
           near(lhs.z, rhs.z, epsilon);
}

std::filesystem::path fixtureDir()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "raven_assimp_import_tests";
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path writeInstancedGltf(const std::filesystem::path& dir)
{
    const std::filesystem::path binaryPath = dir / "instanced.bin";
    const float positions[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    const uint16_t indices[] = {0, 1, 2};
    {
        std::ofstream binary(binaryPath, std::ios::binary);
        binary.write(reinterpret_cast<const char*>(positions),
                     sizeof(positions));
        binary.write(reinterpret_cast<const char*>(indices), sizeof(indices));
    }

    const std::filesystem::path gltfPath = dir / "instanced.gltf";
    std::ofstream gltf(gltfPath);
    gltf << R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "instanced.bin", "byteLength": 42}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3,
     "type": "VEC3", "min": [0,0,0], "max": [1,1,0]},
    {"bufferView": 1, "componentType": 5123, "count": 3,
     "type": "SCALAR"}
  ],
  "materials": [{"name": "MAT_Test"}],
  "meshes": [{"name": "Triangle", "primitives": [
    {"attributes": {"POSITION": 0}, "indices": 1, "material": 0}
  ]}],
  "nodes": [
    {"name": "Right", "mesh": 0, "translation": [1,0,0]},
    {"name": "Left", "mesh": 0, "translation": [-1,2,0]}
  ],
  "scenes": [{"nodes": [0,1]}],
  "scene": 0
})";
    return gltfPath;
}

std::vector<glm::vec3> positions(const eng::detail::ImportedModelData& model)
{
    std::vector<glm::vec3> result;
    for (const auto& submesh : model.submeshes)
        for (const auto& vertex : submesh.vertices)
            result.push_back(vertex.position);
    return result;
}

bool contains(const std::vector<glm::vec3>& values, glm::vec3 expected)
{
    return std::any_of(values.begin(), values.end(),
                       [&](glm::vec3 value) { return near(value, expected); });
}

void test_common_formats_are_built_in()
{
    const std::vector<std::string> extensions =
        eng::detail::supportedAssimpModelExtensions();
    for (const char* required :
         {".glb", ".gltf", ".fbx", ".obj", ".dae", ".stl", ".ply",
          ".3ds"})
        require(std::find(extensions.begin(), extensions.end(), required) !=
                    extensions.end(),
                "common model extension is absent");
    require(eng::detail::assimpSupportsModelFile("hero.FBX"),
            "extension support is not case insensitive");
    require(!eng::detail::assimpSupportsModelFile("scene.usd"),
            "experimental USD unexpectedly entered production formats");
}

void test_gltf_flattens_node_instances_and_reports_budget()
{
    const std::filesystem::path path = writeInstancedGltf(fixtureDir());
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    require(report.succeeded(), "successful import report is not successful");
    require(report.sourceMeshes == 1 && report.submeshes == 2,
            "node instances were not emitted as two submeshes");
    require(report.vertices == 6 && report.triangles == 2,
            "final geometry counts are wrong");
    require(report.materials == 1, "material count is wrong");
    require(model.collisionVertices.size() == 6 &&
                model.collisionIndices.size() == 6,
            "collision geometry does not cover every instance");
    require(model.collisionIndices[3] >= 3,
            "second collision submesh did not offset indices");
    size_t collisionVertex = 0;
    size_t collisionIndex = 0;
    uint32_t collisionBase = 0;
    for (const auto& submesh : model.submeshes) {
        for (const auto& vertex : submesh.vertices)
            require(near(model.collisionVertices[collisionVertex++],
                         vertex.position),
                    "render and collision vertex streams diverged");
        for (uint32_t index : submesh.indices)
            require(model.collisionIndices[collisionIndex++] ==
                        collisionBase + index,
                    "render and collision index streams diverged");
        collisionBase += uint32_t(submesh.vertices.size());
    }
    const std::vector<glm::vec3> importedPositions = positions(model);
    require(contains(importedPositions, {1.0f, 0.0f, 0.0f}) &&
                contains(importedPositions, {-1.0f, 2.0f, 0.0f}) &&
                contains(importedPositions, {-1.0f, 3.0f, 0.0f}),
            "glTF node transforms were not baked into engine geometry");
    require(model.submeshes[0].sourceMaterial == "MAT_Test" &&
                model.submeshes[1].sourceMaterial == "MAT_Test",
            "source material slots were not retained");
    require(report.sourceMaterials ==
                std::vector<std::string>({"MAT_Test", "MAT_Test"}),
            "material slot order is absent from import report");
    require(!report.canonicalPivotStandard,
            "source-pivot import claimed canonical spatial standard");

    options = {};
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    require(report.appliedPivot == eng::PivotMode::BottomCenter &&
                near(report.appliedMetresPerSourceUnit, 1.0f) &&
                report.canonicalPivotStandard &&
                eng::modelImportMeetsSpatialStandard(report.finalBounds),
            "default import did not enforce bottom-centred ground pivot");

    options.pivot = eng::PivotMode::Source;
    options.limits.maxTriangles = 1;
    require(!eng::detail::importStaticModel(path, options, model, report),
            "triangle budget accepted instanced geometry over limit");
    require(report.error.find("triangle budget exceeded") != std::string::npos,
            "budget rejection lacks actionable diagnostic");

    options = {};
    options.limits.maxSourceBytes = 1;
    require(!eng::detail::importStaticModel(path, options, model, report) &&
                report.error.find("source file budget exceeded") !=
                    std::string::npos,
            "source-byte budget was not enforced before parsing");
    options = {};
    options.limits.maxNodes = 1;
    require(!eng::detail::importStaticModel(path, options, model, report) &&
                report.error.find("node budget exceeded") != std::string::npos,
            "node budget was not enforced");
}

void test_obj_preserves_legacy_v_flip_and_override()
{
    const std::filesystem::path path = fixtureDir() / "uv.obj";
    {
        std::ofstream obj(path);
        obj << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "vt 0.25 0.20\n"
               "vt 0.75 0.20\n"
               "vt 0.25 0.60\n"
               "f 1/1 2/2 3/3\n";
    }
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    require(near(model.submeshes[0].vertices[0].texcoord.y, 0.8f),
            "OBJ format default did not preserve legacy V flip");

    options.texcoordV = eng::TexcoordVMode::Preserve;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    require(near(model.submeshes[0].vertices[0].texcoord.y, 0.2f),
            "explicit UV preservation was ignored");
}

void test_obj_rgba_vertex_extension_preserves_alpha()
{
    const std::filesystem::path path = fixtureDir() / "rgba.obj";
    {
        std::ofstream obj(path);
        obj << "v 0 0 0 1 0 0 0.25\n"
               "v 1 0 0 0 1 0 0.50\n"
               "v 0 1 0 0 0 1 0.75\n"
               "f 1 2 3\n";
    }
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    std::vector<float> alpha;
    for (const auto& vertex : model.submeshes[0].vertices)
        alpha.push_back(vertex.colour.a);
    std::sort(alpha.begin(), alpha.end());
    require(alpha.size() == 3 && near(alpha[0], 0.25f) &&
                near(alpha[1], 0.50f) && near(alpha[2], 0.75f),
            "legacy OBJ RGBA vertex alpha was lost");
}

void test_identical_named_materials_keep_distinct_slots()
{
    const std::filesystem::path dir = fixtureDir();
    const std::filesystem::path materialPath = dir / "named.mtl";
    {
        std::ofstream material(materialPath);
        material << "newmtl MAT_A\nKd 1 1 1\n"
                    "newmtl MAT_B\nKd 1 1 1\n";
    }
    const std::filesystem::path path = dir / "named.obj";
    {
        std::ofstream obj(path);
        obj << "mtllib named.mtl\n"
               "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
               "v 2 0 0\nv 3 0 0\nv 2 1 0\n"
               "usemtl MAT_A\nf 1 2 3\n"
               "usemtl MAT_B\nf 4 5 6\n";
    }
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    std::vector<std::string> materials = report.sourceMaterials;
    std::sort(materials.begin(), materials.end());
    require(materials == std::vector<std::string>({"MAT_A", "MAT_B"}),
            "identically configured named materials were merged");
}

void test_stl_and_ply_geometry_and_colour()
{
    const std::filesystem::path dir = fixtureDir();
    const std::filesystem::path stlPath = dir / "triangle.stl";
    {
        std::ofstream stl(stlPath);
        stl << "solid triangle\n"
               "facet normal 0 0 1\nouter loop\n"
               "vertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
               "endloop\nendfacet\nendsolid triangle\n";
    }
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(stlPath, options, model, report),
            report.error.c_str());
    require(report.triangles == 1 && report.vertices == 3,
            "ASCII STL did not import one triangle");

    const std::filesystem::path plyPath = dir / "colour.ply";
    {
        std::ofstream ply(plyPath);
        ply << "ply\nformat ascii 1.0\n"
               "element vertex 3\n"
               "property float x\nproperty float y\nproperty float z\n"
               "property uchar red\nproperty uchar green\n"
               "property uchar blue\nproperty uchar alpha\n"
               "element face 1\nproperty list uchar int vertex_indices\n"
               "end_header\n"
               "0 0 0 255 0 0 255\n"
               "1 0 0 0 255 0 255\n"
               "0 1 0 0 0 255 255\n"
               "3 0 1 2\n";
    }
    require(eng::detail::importStaticModel(plyPath, options, model, report),
            report.error.c_str());
    const glm::vec4 colour = model.submeshes[0].vertices[0].colour;
    require(near(colour.r, 1.0f) && near(colour.g, 0.0f) &&
                near(colour.b, 0.0f) && near(colour.a, 1.0f),
            "PLY vertex colour was not retained");
}

void test_non_manifold_geometry_is_rejected()
{
    const std::filesystem::path path = fixtureDir() / "non_manifold.obj";
    {
        std::ofstream obj(path);
        obj << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "v 0 -1 0\n"
               "v 0 2 0\n"
               "f 1 2 3\n"
               "f 2 1 4\n"
               "f 1 2 5\n";
    }
    eng::ModelImportOptions options;
    options.pivot = eng::PivotMode::Source;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(!eng::detail::importStaticModel(path, options, model, report),
            "non-manifold mesh passed validation");
    require(report.error.find("non-manifold edge") != std::string::npos,
            "non-manifold rejection lacks actionable diagnostic");
}

void test_spatial_scale_precedes_cleanup_and_opposite_faces_survive()
{
    const std::filesystem::path path = fixtureDir() / "tiny_two_sided.obj";
    {
        std::ofstream obj(path);
        obj << "v 0 0 0\n"
               "v 0.000001 0 0\n"
               "v 0 0.000001 0\n"
               "f 1 2 3\n"
               "f 1 3 2\n";
    }
    eng::ModelImportOptions options;
    options.metresPerSourceUnit = 1'000'000.0f;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(path, options, model, report),
            report.error.c_str());
    require(report.triangles == 2 && report.removedDuplicateTriangles == 0 &&
                report.removedDegenerateTriangles == 0,
            "scaled tiny geometry or intentional backface was removed");
    require(eng::modelImportMeetsSpatialStandard(report.finalBounds),
            "scaled fixture did not meet canonical spatial standard");
}

// True when a mesh's .meta opts it out of static import -- the skinned rigs,
// which gltf2ozz owns. A substring match rather than a TOML parse because this
// test links the model importer and nothing else, and pulling a parser in to
// read one boolean would be the larger change.
bool skinnedRigOptOut(const std::filesystem::path& model)
{
    std::filesystem::path meta = model;
    meta += ".meta";
    std::ifstream file(meta);
    if (!file)
        return false;
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    return text.find("skip = true") != std::string::npos;
}

void test_shipped_models_pass_import_gate()
{
#ifdef PROJECT_SOURCE_DIR
    const std::filesystem::path root =
        std::filesystem::path(PROJECT_SOURCE_DIR) / "assets" / "meshes";
    require(std::filesystem::is_directory(root),
            "shipped mesh directory is missing");
    size_t imported = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() ||
            !eng::detail::assimpSupportsModelFile(entry.path()))
            continue;
        // Deforming assets have their own skeleton/bind-pose production gate
        // in SkeletalAnimationTests; static importer must continue rejecting
        // them rather than silently discarding skin and clips.
        //
        // Recognised by the sidecar that already states it -- `skip = true`,
        // which is also what keeps them out of the conditioning pipeline --
        // rather than by filename. There are two such rigs now (the arms and
        // the actor humanoid) and adding a third should not require editing a
        // test to keep the suite green.
        if (skinnedRigOptOut(entry.path()))
            continue;
        eng::ModelImportOptions options;
        eng::detail::ImportedModelData model;
        eng::ModelImportReport report;
        if (!eng::detail::importStaticModel(entry.path(), options, model,
                                            report)) {
            std::cerr << entry.path() << ": " << report.error << '\n';
            require(false, "shipped model failed import validation");
        }
        require(report.canonicalPivotStandard &&
                    eng::modelImportMeetsSpatialStandard(report.finalBounds),
                "shipped model violated canonical spatial standard");
        ++imported;
    }
    require(imported > 0, "shipped model gate imported no files");
#endif
}

void test_optional_external_model()
{
    const char* source = std::getenv("RAVEN_TEST_MODEL");
    if (!source)
        return;
    eng::ModelImportOptions options;
    eng::detail::ImportedModelData model;
    eng::ModelImportReport report;
    require(eng::detail::importStaticModel(source, options, model, report),
            report.error.c_str());
    std::cout << "external fixture: " << report.vertices << " vertices, "
              << report.triangles << " triangles, " << report.submeshes
              << " submeshes, bounds [" << report.finalBounds.min.x << ", "
              << report.finalBounds.min.y << ", " << report.finalBounds.min.z
              << "] to [" << report.finalBounds.max.x << ", "
              << report.finalBounds.max.y << ", " << report.finalBounds.max.z
              << "]\n";
}

} // namespace

int main()
{
    test_common_formats_are_built_in();
    test_gltf_flattens_node_instances_and_reports_budget();
    test_obj_preserves_legacy_v_flip_and_override();
    test_obj_rgba_vertex_extension_preserves_alpha();
    test_identical_named_materials_keep_distinct_slots();
    test_stl_and_ply_geometry_and_colour();
    test_non_manifold_geometry_is_rejected();
    test_spatial_scale_precedes_cleanup_and_opposite_faces_survive();
    test_shipped_models_pass_import_gate();
    test_optional_external_model();
    std::cout << "AssimpModelImportTests: OK\n";
    return 0;
}
