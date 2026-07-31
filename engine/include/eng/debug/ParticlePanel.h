#pragma once
#include <eng/DebugTools.h> // eng::DebugTools, the panel host this joins
#include <eng/Handles.h>
#include <eng/particles/ParticleEffectDesc.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng {

class Renderer;
class ParticleLibrary;

// Live particle authoring: browse the effect library, edit an effect field by
// field, and spawn it into whatever scene is on screen.
//
// It lives in the engine for the same reason the surface panels do. Everything
// it touches -- ParticleLibrary, the texture import, Renderer::spawnParticles
// -- is engine machinery, and a game that reimplemented this panel would be
// reimplementing engine tooling. Any application gets it by constructing one
// and calling install(); the panel learns nothing about what the application
// is.
//
// Two things are deliberately NOT here. There is no delete: an effect removed
// from under a live handle is a crash the panel cannot see coming, and the
// authored file is one text editor away. And an existing effect's name is
// read-only, because the Renderer keys effects by name and a rename would
// strand every instance already holding the old id; Clone exists for that
// instead.
class ParticlePanel {
public:
    // Refreshed by the application every frame, so nothing dangles across a
    // level rebuild. Either may be null; the panel then says so rather than
    // dereferencing it.
    void setSources(Renderer* renderer, ParticleLibrary* library);

    // Registers the tab. Call once at startup.
    void install(DebugTools& tools, PanelGroup group = PanelGroup::Content);

    // Retires the panel's own spawns when their effect is one-shot and drops
    // handles the scene took away. Call once per frame; cheap when idle.
    void update(float dt);

    // Release every effect this panel spawned. The application must call it
    // before a scene clear: the handles point into the scene graph that is
    // about to go away.
    void releaseSpawns();

private:
    void draw();
    void drawLibrary();
    void drawSpawn();
    void drawEditor();
    void drawEmitters(ParticleEffectDesc& desc, bool& dirty);
    void drawRamps(ParticleEffectDesc& desc, bool& dirty);
    void drawTexturePicker(ParticleEffectDesc& desc, bool& dirty);

    // Where a spawn lands. "Ahead" needs no scene knowledge: the camera's
    // world-space eye and forward are recovered from Renderer::cameraViewProj,
    // which every application already maintains.
    glm::vec3 spawnPoint() const;

    // Pushes the edited desc back into the Renderer. Same call the TOML
    // hot-reload uses, so a panel edit and a file edit land identically.
    void apply();

    Renderer* mRenderer = nullptr;
    ParticleLibrary* mLibrary = nullptr;

    int mSelected = 0;
    char mFilter[64] = {};
    char mTextureFilter[64] = {};
    char mCloneName[64] = {};

    // --- spawn settings ---------------------------------------------------
    int mPlacement = 0;  // 0 = ahead of the camera, 1 = a fixed point
    float mAhead = 3.0f; // metres along the view direction
    float mDrop = 0.0f;  // metres below it, so a ground effect sits
    glm::vec3 mFixed{0.0f};
    ParticleSpawnOptions mOptions;
    bool mAutoDespawn = true; // retire one-shots once their burst is over

    struct Spawn {
        ParticlesHandle handle;
        float age = 0.0f;
        float ttl = 0.0f; // 0 = never retired automatically
        std::string effect;
    };
    std::vector<Spawn> mSpawns;
};

} // namespace eng
