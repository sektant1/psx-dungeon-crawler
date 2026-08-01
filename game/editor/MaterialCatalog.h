#pragma once

#include <string>
#include <vector>

namespace ed {

// What a material needs from the thing it is put on.
//
// The editor's material list was 126 names in one flat column, and applying any
// of them to any entity was one click. Most of those combinations do not
// render: a particle material wants a per-instance stream a static mesh has
// not got, a bloom pass wants the framebuffer, and an atlas material wants UVs
// that sit inside its sheet -- put it on a generated quad and the whole sheet
// stretches across one face, which is how a wall ends up wearing a collage of
// unrelated stone.
//
// None of that was visible before the click, and several of them fail *quietly*
// -- the mesh renders black, or vanishes, or comes back as a checkerboard on
// the next load. The class is read out of the material script itself, so this
// stays true when a material is edited rather than encoding a list of names
// somebody has to remember to update.
enum class MaterialClass {
    // Ordinary lit or unlit world surface (PSX_VS_Lit / PSX_VS_Unlit) whose
    // texture wraps. Goes on anything.
    Surface,
    // The same, but its texture is an atlas sampled with clamp: the mesh's UVs
    // have to land inside the sheet's regions. Correct on the kit pieces it was
    // authored for, wrong on anything with 0..1 UVs.
    Atlas,
    // An animated VFX surface -- liquid, portal (PixelVfx/*). Written for a
    // flat quad carrying 0..1 UVs; the flow and swirl are computed from those.
    VfxSurface,
    // Needs the per-instance stream the particle system supplies. On a static
    // mesh it draws nothing, or garbage.
    Particle,
    // Billboarded sprite or decal geometry the engine generates itself.
    Sprite,
    // Full-screen pass driven by the compositor. Never belongs on an entity.
    PostProcess,
    // The editor's own: ghosts, checkerboards, thumbnails. Not shipped content.
    EditorOnly,
    // The script did not say enough to place it. Offered, with a warning.
    Unknown,
};

const char* materialClassName(MaterialClass klass);

struct MaterialInfo {
    std::string name;
    MaterialClass klass = MaterialClass::Unknown;
    std::string vertexProgram;
    std::string fragmentProgram;
    std::string texture;
    // True when the texture is sampled with clamp, which is what makes an
    // atlas an atlas: wrapping UVs outside 0..1 would smear the edge pixel.
    bool clamped = false;
    bool twoSided = false;
};

// Every `material <Name>` in a .material script, classified. A file that does
// not parse yields what it managed to read: a partial list beats a dialog.
std::vector<MaterialInfo> parseMaterialScript(const std::string& path);

// The same across a directory of scripts, sorted by name.
std::vector<MaterialInfo> loadMaterialCatalog(const std::string& directory);

// What a mesh offers a material.
//
// Derived from the material a kit piece was authored with, not guessed from the
// file: the piece declares what its UVs are for, and that declaration is the
// only reliable source. A piece authored against an atlas has atlas UVs.
enum class MeshKind {
    // Kit geometry whose UVs index an atlas sheet.
    AtlasUv,
    // A prop or model with its own wrapping texture and ordinary UVs.
    Tiling,
    // A quad or box the engine generated, carrying 0..1 UVs over each face.
    Generated,
    // Nothing known about it.
    Unknown,
};

enum class Fit {
    Good,    // renders as intended
    Risky,   // renders, but probably not as the author expects
    Broken,  // does not render, or renders as garbage
};

struct MaterialAdvice {
    Fit fit = Fit::Good;
    // Empty when the fit is Good. Says what will happen, not "incompatible":
    // an author who is told *why* can decide to do it anyway.
    std::string reason;
};

// Whether `klass` belongs on `mesh`.
MaterialAdvice materialFits(MaterialClass klass, MeshKind mesh);

// The mesh kind implied by the material a piece is authored with.
MeshKind meshKindForMaterial(MaterialClass klass);

// True for the classes that never belong on a scene entity, whatever the mesh.
// The editor hides these behind a toggle rather than listing them beside the
// materials somebody is actually choosing between.
bool isEntityMaterial(MaterialClass klass);

} // namespace ed
