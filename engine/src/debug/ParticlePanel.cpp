#include <eng/debug/ParticlePanel.h>

#include <eng/Renderer.h>
#include <eng/particles/ParticleLibrary.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

namespace eng {

namespace {

bool section(const char* label, bool open = true)
{
    return ImGui::CollapsingHeader(label,
                                   open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}

// Case-insensitive substring, so the filter boxes behave the way every asset
// browser does rather than the way strstr does.
bool matches(const std::string& haystack, const char* needle)
{
    if (!needle || !*needle)
        return true;
    const size_t n = std::strlen(needle);
    if (n > haystack.size())
        return false;
    for (size_t i = 0; i + n <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < n && std::tolower((unsigned char)haystack[i + j]) ==
                            std::tolower((unsigned char)needle[j]))
            ++j;
        if (j == n)
            return true;
    }
    return false;
}

// How long a one-shot spawn can still be producing pixels: the emission window
// plus the longest a particle from it can live. The panel retires its own
// spawns on this rather than polling the simulation, because a handle whose
// particles have aged out is still a live handle and leaking them is how a
// tuning session ends up at the pool capacity.
float effectLifetime(const ParticleEffectDesc& desc,
                     const ParticleSpawnOptions& options)
{
    float ttl = 0.0f;
    for (const ParticleEmitterDesc& e : desc.emitters)
        ttl = std::max(ttl, e.ttlMax);
    const float burst = desc.loop ? 0.0f : 0.25f;
    return burst + ttl * std::max(0.001f, options.lifetimeScale) + 0.5f;
}

const char* kRoles[] = {"critical", "gameplay", "feedback", "ambient"};
const char* kCollide[] = {"none", "die", "bounce", "decal"};
const char* kModes[] = {"sprite", "voxel"};

} // namespace

void ParticlePanel::setSources(Renderer* renderer, ParticleLibrary* library)
{
    mRenderer = renderer;
    mLibrary = library;
}

void ParticlePanel::install(DebugTools& tools, PanelGroup group)
{
    tools.addPanel("Particles", [this] { draw(); }, group);
}

void ParticlePanel::update(float dt)
{
    if (!mRenderer)
        return;
    if (!std::isfinite(dt) || dt < 0.0f)
        dt = 0.0f;

    for (size_t i = mSpawns.size(); i-- > 0;) {
        Spawn& s = mSpawns[i];
        s.age += dt;
        if (s.ttl <= 0.0f || s.age < s.ttl)
            continue;
        mRenderer->despawnParticles(s.handle);
        mSpawns.erase(mSpawns.begin() + long(i));
    }
}

void ParticlePanel::releaseSpawns()
{
    if (mRenderer)
        for (const Spawn& s : mSpawns)
            mRenderer->despawnParticles(s.handle);
    mSpawns.clear();
}

glm::vec3 ParticlePanel::spawnPoint() const
{
    if (mPlacement == 1 || !mRenderer)
        return mFixed;

    // Unprojecting the clip-space centre at two depths gives the eye ray
    // without the Renderer having to expose a camera transform it does not
    // otherwise need to.
    const glm::mat4 inv = glm::inverse(mRenderer->cameraViewProj());
    const glm::vec4 nearP = inv * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
    const glm::vec4 farP = inv * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    if (std::abs(nearP.w) < 1e-6f || std::abs(farP.w) < 1e-6f)
        return mFixed;
    const glm::vec3 eye = glm::vec3(nearP) / nearP.w;
    const glm::vec3 forward = glm::normalize(glm::vec3(farP) / farP.w - eye);
    return eye + forward * mAhead - glm::vec3(0.0f, mDrop, 0.0f);
}

void ParticlePanel::apply()
{
    if (mRenderer && mLibrary)
        mLibrary->reregister(*mRenderer, size_t(mSelected));
}

// ---------------------------------------------------------------- library ---

void ParticlePanel::drawLibrary()
{
    std::vector<ParticleEffectDesc>& descs = mLibrary->descs();

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter effects", mFilter,
                             sizeof(mFilter));

    if (ImGui::BeginListBox("##effects", ImVec2(-FLT_MIN, 140.0f))) {
        for (int i = 0; i < int(descs.size()); ++i) {
            if (!matches(descs[size_t(i)].name, mFilter))
                continue;
            if (ImGui::Selectable(descs[size_t(i)].name.c_str(),
                                  i == mSelected))
                mSelected = i;
        }
        ImGui::EndListBox();
    }

    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##clonename", "new effect name", mCloneName,
                             sizeof(mCloneName));
    ImGui::SameLine();
    const bool nameable = mCloneName[0] != '\0';
    ImGui::BeginDisabled(!nameable);
    if (ImGui::Button("Clone")) {
        ParticleEffectDesc copy = descs[size_t(mSelected)];
        copy.name = mCloneName;
        mSelected = int(mLibrary->add(*mRenderer, std::move(copy)));
        mCloneName[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save to TOML"))
        mLibrary->save(mLibrary->path());
    if (!mLibrary->path().empty()) {
        ImGui::TextDisabled("%s", mLibrary->path().c_str());
        ImGui::SetItemTooltip("Saving rewrites this file: values survive, "
                              "comments and key order do not.");
    }
}

// ------------------------------------------------------------------ spawn ---

void ParticlePanel::drawSpawn()
{
    const ParticleEffectDesc& desc = mLibrary->descs()[size_t(mSelected)];

    ImGui::RadioButton("Ahead of view", &mPlacement, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Fixed point", &mPlacement, 1);
    if (mPlacement == 0) {
        ImGui::SliderFloat("Distance", &mAhead, 0.5f, 20.0f, "%.1f m");
        ImGui::SliderFloat("Drop", &mDrop, 0.0f, 4.0f, "%.2f m");
        ImGui::SetItemTooltip("Metres below the view ray, so a ground effect "
                              "lands on the floor instead of at eye height.");
    }
    else {
        ImGui::DragFloat3("World", &mFixed.x, 0.05f);
        if (ImGui::Button("Here", ImVec2(-FLT_MIN, 0))) {
            const int keep = mPlacement;
            mPlacement = 0;
            mFixed = spawnPoint();
            mPlacement = keep;
        }
    }

    if (section("Spawn overrides", false)) {
        ImGui::PushID("opts");
        ImGui::SliderFloat("Size", &mOptions.sizeScale, 0.05f, 8.0f, "%.2fx");
        ImGui::SliderFloat("Amount", &mOptions.amountScale, 0.0f, 8.0f,
                           "%.2fx");
        ImGui::SliderFloat("Lifetime", &mOptions.lifetimeScale, 0.05f, 8.0f,
                           "%.2fx");
        ImGui::SliderFloat("Speed", &mOptions.speedScale, 0.0f, 8.0f, "%.2fx");
        ImGui::SliderFloat("Emitter radius", &mOptions.emitterRadius, 0.0f,
                           6.0f, "%.2f m");
        ImGui::ColorEdit4("Tint", &mOptions.colourTint.x,
                          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        if (ImGui::Button("Reset overrides", ImVec2(-FLT_MIN, 0)))
            mOptions = ParticleSpawnOptions{};
        ImGui::PopID();
    }

    const float width = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    if (ImGui::Button("Spawn", ImVec2(width, 0))) {
        const ParticlesHandle h =
            mRenderer->spawnParticles(desc.name, spawnPoint(), mOptions);
        if (h.valid()) {
            const bool retire = mAutoDespawn && !desc.loop;
            mSpawns.push_back({h, 0.0f,
                               retire ? effectLifetime(desc, mOptions) : 0.0f,
                               desc.name});
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop emitting", ImVec2(width, 0)))
        for (const Spawn& s : mSpawns)
            mRenderer->stopParticles(s.handle);

    if (ImGui::Button("Despawn all", ImVec2(-FLT_MIN, 0)))
        releaseSpawns();

    ImGui::Checkbox("Retire one-shots automatically", &mAutoDespawn);
    ImGui::Text("panel spawns: %zu   live particles: %u", mSpawns.size(),
                mRenderer->liveParticleCount());
}

// -------------------------------------------------------------- textures ---

void ParticlePanel::drawTexturePicker(ParticleEffectDesc& desc, bool& dirty)
{
    const std::vector<ParticleTextureDesc>& textures =
        mRenderer->particleTextures();

    const char* current =
        desc.texture.empty() ? "(material only)" : desc.texture.c_str();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##texture", current)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##texfilter", "filter textures",
                                 mTextureFilter, sizeof(mTextureFilter));
        if (ImGui::Selectable("(material only)", desc.texture.empty())) {
            desc.texture.clear();
            dirty = true;
        }
        for (const ParticleTextureDesc& t : textures) {
            if (!matches(t.stem, mTextureFilter))
                continue;
            if (ImGui::Selectable(t.stem.c_str(), t.stem == desc.texture)) {
                desc.texture = t.stem;
                dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("A texture stem wins over the material below and\n"
                          "carries the blend mode and the flipbook with it.");

    // What the chosen strip actually is. Without this the picker is 300 names
    // and no way to tell an eight-frame spark from a twelve-frame coil.
    for (const ParticleTextureDesc& t : textures) {
        if (t.stem != desc.texture)
            continue;
        ImGui::TextDisabled(
            "%s  %d frames @ %.0f fps  cell %d/%d  row %d  %s",
            t.file.empty() ? "(own file)" : t.file.c_str(),
            t.flipbook.frameCount(), double(t.flipbook.fps),
            t.flipbook.sheetCols, t.flipbook.sheetRows, t.flipbook.originRow,
            t.blend == ParticleBlend::Additive ? "additive" : "alpha");
        break;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    char material[128];
    std::snprintf(material, sizeof(material), "%s", desc.material.c_str());
    if (ImGui::InputTextWithHint("##material", "Ogre material", material,
                                 sizeof(material))) {
        desc.material = material;
        dirty = true;
    }

    if (ImGui::Button("Reload texture import", ImVec2(-FLT_MIN, 0))) {
        mRenderer->reloadParticleTextures();
        // Effects hold a resolved material name, so they have to re-resolve
        // before an edited blend mode or flipbook window reaches them.
        mLibrary->reregisterAll(*mRenderer);
    }
    ImGui::SetItemTooltip(
        "Re-scans assets/particles: new PNGs, edited *.toml.");
}

// --------------------------------------------------------------- emitters ---

void ParticlePanel::drawEmitters(ParticleEffectDesc& desc, bool& dirty)
{
    for (size_t i = 0; i < desc.emitters.size(); ++i) {
        ParticleEmitterDesc& e = desc.emitters[i];
        ImGui::PushID(int(i));
        char label[32];
        std::snprintf(label, sizeof(label), "Emitter %zu", i);
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            int shape = e.shape == ParticleEmitterShape::Box ? 1 : 0;
            const char* shapes[] = {"point", "box"};
            if (ImGui::Combo("Shape", &shape, shapes, 2)) {
                e.shape = shape ? ParticleEmitterShape::Box
                                : ParticleEmitterShape::Point;
                dirty = true;
            }
            if (shape)
                dirty |= ImGui::DragFloat3("Box size", &e.boxSize.x, 0.02f,
                                           0.0f, 40.0f);
            dirty |= ImGui::DragFloat3("Offset", &e.position.x, 0.02f);
            dirty |= ImGui::DragFloat3("Direction", &e.direction.x, 0.02f,
                                       -1.0f, 1.0f);
            dirty |= ImGui::SliderFloat("Cone", &e.angleDegrees, 0.0f, 180.0f,
                                        "%.0f deg");
            dirty |= ImGui::SliderFloat("Rate", &e.emissionRate, 0.0f, 400.0f,
                                        "%.1f/s");
            dirty |= ImGui::DragFloatRange2("TTL", &e.ttlMin, &e.ttlMax, 0.01f,
                                            0.01f, 30.0f, "%.2f s", "%.2f s");
            dirty |=
                ImGui::DragFloatRange2("Speed", &e.velocityMin, &e.velocityMax,
                                       0.01f, 0.0f, 60.0f, "%.2f", "%.2f");
            dirty |= ImGui::ColorEdit4("Start colour", &e.startColour.x,
                                       ImGuiColorEditFlags_HDR |
                                           ImGuiColorEditFlags_Float);
            // One emitter is the minimum: an effect with none never registers,
            // and removing the last one would take the effect out of the
            // Renderer under any instance still holding its id.
            ImGui::BeginDisabled(desc.emitters.size() <= 1);
            if (ImGui::Button("Remove emitter")) {
                desc.emitters.erase(desc.emitters.begin() + long(i));
                ImGui::EndDisabled();
                ImGui::TreePop();
                ImGui::PopID();
                dirty = true;
                return;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add emitter", ImVec2(-FLT_MIN, 0))) {
        desc.emitters.push_back(desc.emitters.empty() ? ParticleEmitterDesc{}
                                                      : desc.emitters.back());
        dirty = true;
    }
}

// ------------------------------------------------------------------ ramps ---

void ParticlePanel::drawRamps(ParticleEffectDesc& desc, bool& dirty)
{
    ImGui::TextDisabled("Colour over life (alpha included)");
    for (size_t i = 0; i < desc.colourRamp.size(); ++i) {
        ImGui::PushID(int(100 + i));
        ColourStop& stop = desc.colourRamp[i];
        ImGui::SetNextItemWidth(70.0f);
        dirty |= ImGui::DragFloat("##t", &stop.t, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-40.0f);
        dirty |= ImGui::ColorEdit4("##rgba", &stop.rgba.x,
                                   ImGuiColorEditFlags_HDR |
                                       ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            desc.colourRamp.erase(desc.colourRamp.begin() + long(i));
            ImGui::PopID();
            dirty = true;
            return;
        }
        ImGui::PopID();
    }
    // Six is the interpolator's ceiling; offering a seventh would silently
    // drop it at registration.
    ImGui::BeginDisabled(desc.colourRamp.size() >= 6);
    if (ImGui::Button("Add colour stop", ImVec2(-FLT_MIN, 0))) {
        const float t = desc.colourRamp.empty()
                            ? 0.0f
                            : std::min(1.0f, desc.colourRamp.back().t + 0.25f);
        desc.colourRamp.push_back({t, glm::vec4(1.0f)});
        dirty = true;
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Size over life");
    for (size_t i = 0; i < desc.sizeRamp.size(); ++i) {
        ImGui::PushID(int(200 + i));
        SizeStop& stop = desc.sizeRamp[i];
        ImGui::SetNextItemWidth(70.0f);
        dirty |= ImGui::DragFloat("##t", &stop.t, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-40.0f);
        dirty |= ImGui::DragFloat("##scale", &stop.scale, 0.01f, 0.0f, 16.0f,
                                  "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            desc.sizeRamp.erase(desc.sizeRamp.begin() + long(i));
            ImGui::PopID();
            dirty = true;
            return;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add size stop", ImVec2(-FLT_MIN, 0))) {
        const float t = desc.sizeRamp.empty()
                            ? 0.0f
                            : std::min(1.0f, desc.sizeRamp.back().t + 0.5f);
        desc.sizeRamp.push_back({t, 1.0f});
        dirty = true;
    }
}

// ----------------------------------------------------------------- editor ---

void ParticlePanel::drawEditor()
{
    ParticleEffectDesc& desc = mLibrary->descs()[size_t(mSelected)];
    bool dirty = false;

    if (section("Presentation")) {
        ImGui::PushID("pres");
        drawTexturePicker(desc, dirty);
        int mode = desc.renderMode == ParticleRenderMode::Voxel ? 1 : 0;
        if (ImGui::Combo("Render", &mode, kModes, 2)) {
            desc.renderMode =
                mode ? ParticleRenderMode::Voxel : ParticleRenderMode::Sprite;
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Width", &desc.baseWidth, 0.002f, 0.001f,
                                  8.0f, "%.3f m");
        dirty |= ImGui::DragFloat("Height", &desc.baseHeight, 0.002f, 0.001f,
                                  8.0f, "%.3f m");
        dirty |= ImGui::SliderFloat("Rotation jitter", &desc.rotationJitterDeg,
                                    0.0f, 180.0f, "%.0f deg");
        dirty |= ImGui::SliderFloat("Hue jitter", &desc.hueJitter, 0.0f, 1.0f);
        dirty |=
            ImGui::SliderFloat("Scale jitter", &desc.scaleJitter, 0.0f, 1.0f);
        ImGui::PopID();
    }

    if (section("Simulation")) {
        ImGui::PushID("sim");
        dirty |= ImGui::DragInt("Quota", &desc.quota, 1.0f, 1, 8192);
        ImGui::SetItemTooltip("Particles this effect may hold before quality "
                              "scaling. The batch is sized from it.");
        dirty |= ImGui::Checkbox("Loop", &desc.loop);
        if (!desc.loop) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            dirty |= ImGui::DragFloat("Burst", &desc.burstCount, 1.0f, 0.0f,
                                      4096.0f, "%.0f");
        }
        dirty |= ImGui::Checkbox("Local space", &desc.localSpace);
        ImGui::SetItemTooltip("On: particles follow the emitter node.\n"
                              "Off: they are left behind in world space.");
        dirty |= ImGui::DragFloat3("Acceleration", &desc.acceleration.x, 0.05f,
                                   -60.0f, 60.0f);
        dirty |= ImGui::SliderFloat("Drag", &desc.drag, 0.0f, 12.0f, "%.2f /s");

        int role = int(desc.visualRole);
        if (ImGui::Combo("Visual role", &role, kRoles, 4)) {
            desc.visualRole = ParticleVisualRole(role);
            dirty = true;
        }
        ImGui::SetItemTooltip("How far quality scaling may cut this effect.");
        dirty |= ImGui::SliderFloat("Quality weight", &desc.qualityWeight, 0.0f,
                                    1.0f);

        int collide = int(desc.collideResponse);
        if (ImGui::Combo("Collide", &collide, kCollide, 4)) {
            desc.collideResponse = ParticleCollideResponse(collide);
            dirty = true;
        }
        if (desc.collideResponse == ParticleCollideResponse::Bounce) {
            dirty |= ImGui::SliderFloat("Restitution", &desc.restitution, 0.0f,
                                        1.0f);
            dirty |= ImGui::SliderFloat("Friction", &desc.friction, 0.0f, 1.0f);
        }
        if (desc.collideResponse == ParticleCollideResponse::Decal) {
            char profile[64];
            std::snprintf(profile, sizeof(profile), "%s",
                          desc.decalProfile.c_str());
            if (ImGui::InputTextWithHint("Decal", "profile id", profile,
                                         sizeof(profile))) {
                desc.decalProfile = profile;
                dirty = true;
            }
        }
        ImGui::PopID();
    }

    if (section("Emitters")) {
        ImGui::PushID("em");
        drawEmitters(desc, dirty);
        ImGui::PopID();
    }

    if (section("Ramps")) {
        ImGui::PushID("ramp");
        drawRamps(desc, dirty);
        ImGui::PopID();
    }

    // One apply per frame, after every widget has had its say. Re-registering
    // inside a widget callback would rebuild the batch mid-draw for each of a
    // dozen sliders being dragged at once.
    if (dirty)
        apply();
}

void ParticlePanel::draw()
{
    if (!mRenderer || !mLibrary) {
        ImGui::TextDisabled("Renderer or particle library unavailable.");
        return;
    }
    if (mLibrary->descs().empty()) {
        ImGui::TextDisabled("No effects loaded. Point ParticleLibrary::load at "
                            "a particles.toml first.");
        return;
    }
    mSelected = std::clamp(mSelected, 0, int(mLibrary->descs().size()) - 1);

    if (section("Library")) {
        ImGui::PushID("lib");
        drawLibrary();
        ImGui::PopID();
    }
    if (section("Spawn")) {
        ImGui::PushID("spawn");
        drawSpawn();
        ImGui::PopID();
    }
    ImGui::SeparatorText(mLibrary->descs()[size_t(mSelected)].name.c_str());
    drawEditor();
}

} // namespace eng
