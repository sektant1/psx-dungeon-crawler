#include <eng/debug/ClipPanel.h>

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Systems.h> // clipTrackTarget, shared with the clip player
#include <eng/ecs/World.h>
#include <eng/ecs/components/Clip.h>
#include <eng/ecs/components/Name.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace eng {
namespace {

using ecs::Clip;
using ecs::ClipEase;
using ecs::ClipKey;
using ecs::ClipMode;
using ecs::ClipTrack;
using ecs::ComponentType;
using ecs::FieldType;

// The field types a clip can drive. The picker filters on this rather than
// listing everything and failing at resolve time: a dropdown that offers a
// String is a dropdown that teaches the wrong thing.
bool animatable(FieldType type)
{
    return type == FieldType::Float || type == FieldType::Vec3 ||
           type == FieldType::Colour || type == FieldType::Bool ||
           type == FieldType::Int;
}

// How many of a key's three components a field actually uses. Drives how many
// drag boxes the value editor shows, so a Float track does not offer to edit
// two numbers that go nowhere.
int width(FieldType type)
{
    return (type == FieldType::Vec3 || type == FieldType::Colour) ? 3 : 1;
}

// How an entity is named in the picker: its Name, or its raw id when it has
// none. One helper because the combo's preview and its rows must agree -- when
// they were two copies, the preview and the row for the same entity could be
// formatted differently and read as two different things.
std::string entityLabel(const entt::registry& reg, entt::entity e)
{
    if (e == entt::null || !reg.valid(e))
        return "(none)";
    const auto* name = reg.try_get<ecs::Name>(e);
    if (name && !name->value.empty())
        return name->value;
    return "entity " + std::to_string(entt::to_integral(e));
}

// Keys are stored in time order -- clipSystem's sampler scans forward and reads
// an out-of-order track as a jumble. Every edit that can move a key in time
// ends by calling this, rather than each site repeating the comparator.
void sortKeys(std::vector<ClipKey>& keys)
{
    std::stable_sort(keys.begin(), keys.end(),
                     [](const ClipKey& a, const ClipKey& b) { return a.t < b.t; });
}

// The row geometry. One place, so the ruler, the rows and the playhead cannot
// disagree about where a second is.
constexpr float kTrackHeight = 26.0f;
constexpr float kRulerHeight = 22.0f;
constexpr float kLabelWidth = 210.0f;
constexpr float kKeyRadius = 6.0f;

ImU32 colour(ImGuiCol c, float alphaScale = 1.0f)
{
    ImVec4 v = ImGui::GetStyleColorVec4(c);
    v.w *= alphaScale;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// A keyframe, drawn as a diamond. Selected ones are filled, the rest outlined,
// which is the convention every timeline in every DCC uses and therefore the
// one nobody has to be taught.
void drawKeyDiamond(ImDrawList* draw, ImVec2 at, bool active)
{
    const ImU32 fill = active ? IM_COL32(255, 200, 90, 255)
                              : IM_COL32(210, 210, 215, 255);
    const ImVec2 pts[4] = {{at.x, at.y - kKeyRadius},
                           {at.x + kKeyRadius, at.y},
                           {at.x, at.y + kKeyRadius},
                           {at.x - kKeyRadius, at.y}};
    draw->AddConvexPolyFilled(pts, 4, fill);
    draw->AddPolyline(pts, 4, IM_COL32(20, 20, 24, 220), ImDrawFlags_Closed, 1.5f);
}

} // namespace

void ClipPanel::setSources(ecs::World* world, const ecs::ComponentRegistry* types)
{
    mWorld = world;
    mTypes = types;
}

// Selecting a different clip drops any drag in progress: the indices name a
// track and a key in the clip that was selected when the drag started, and
// carried across they would edit whatever sits at those indices in the new one.
//
// One place, so the combo, an external caller (the editor keeping the timeline
// in step with its outliner) and the stale-entity path below cannot each
// forget it.
void ClipPanel::setSelected(entt::entity e)
{
    if (e == mSelected)
        return;
    mSelected = e;
    mDragTrack = mDragKey = -1;
}

void ClipPanel::install(DebugTools& tools, PanelGroup group)
{
    tools.addPanel("Timeline", [this] { draw(); }, group);
}

void ClipPanel::draw()
{
    if (!mWorld || !mTypes) {
        ImGui::TextDisabled("No world attached.");
        return;
    }
    drawEntityList();
    ImGui::Separator();

    entt::registry& reg = mWorld->registry();
    if (mSelected == entt::null || !reg.valid(mSelected) ||
        !reg.all_of<Clip>(mSelected)) {
        ImGui::TextDisabled("Select an entity with a Clip.");
        return;
    }
    drawTransport();
    drawTimeline();
    ImGui::Separator();
    drawAddTrack();
}

void ClipPanel::drawEntityList()
{
    entt::registry& reg = mWorld->registry();

    // The combo shows every entity carrying a Clip. A list rather than a tree:
    // the timeline edits one clip, and which entity it hangs off is the whole
    // of what has to be picked here.
    const bool any = !reg.view<Clip>().empty();

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Clip", entityLabel(reg, mSelected).c_str())) {
        for (const entt::entity e : reg.view<Clip>())
            if (ImGui::Selectable(entityLabel(reg, e).c_str(), e == mSelected))
                setSelected(e);
        ImGui::EndCombo();
    }

    // Where a clip comes from, stated rather than offered.
    //
    // There used to be a "New clip on selected" button here, and it could never
    // fire: the only selection this panel has is the combo, and the combo lists
    // entities that already carry a Clip. A control that cannot work is worse
    // than no control -- it sends someone looking for why their click did
    // nothing. Adding the component is the inspector's job, and it has it.
    if (!any) {
        ImGui::SameLine();
        ImGui::TextDisabled("(no clips in this scene -- add a Clip component "
                            "to an entity in the Inspector)");
    }
}

void ClipPanel::drawTransport()
{
    Clip& clip = mWorld->registry().get<Clip>(mSelected);

    // Play/pause writes `playing` directly and leaves `started` alone, so the
    // panel and a script are pressing the same button rather than two.
    if (ImGui::Button(clip.playing ? "Pause" : "Play")) {
        clip.started = true;
        clip.playing = !clip.playing;
        // Play on a clip sitting at its end rewinds, the way every transport
        // in every tool does. Without this a finished Once clip could not be
        // replayed at all: it would advance one frame, clamp straight back to
        // the end, and latch finished again.
        if (clip.playing && clip.time >= clip.duration) {
            clip.time = 0.0f;
            clip.direction = 1;
        }
        clip.finished = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        clip.playing = false;
        clip.time = 0.0f;
        clip.direction = 1;
        clip.finished = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Duration", &clip.duration, 0.01f, 0.05f, 120.0f, "%.2f s");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    int mode = int(clip.mode);
    if (ImGui::Combo("Mode", &mode, ecs::kClipModeNames, ecs::kClipModeCount))
        clip.mode = ClipMode(mode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Speed", &clip.speed, 0.01f, -4.0f, 4.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox("Autoplay", &clip.autoplay);

    // Scrubbing pauses. A playhead that fights the player for the position is
    // the single most irritating thing a timeline can do.
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##time", &clip.time, 0.0f, clip.duration, "%.3f s"))
        clip.playing = false;
}

void ClipPanel::drawTimeline()
{
    Clip& clip = mWorld->registry().get<Clip>(mSelected);
    const float duration = std::max(clip.duration, 0.05f);

    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragFloat("Zoom", &mZoom, 2.0f, 40.0f, 2000.0f, "%.0f px/s");

    const float rows = float(std::max<std::size_t>(clip.tracks.size(), 1));
    const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 240.0f),
                      kRulerHeight + rows * kTrackHeight + 8.0f);

    ImGui::BeginChild("##timeline", size, ImGuiChildFlags_Border,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float lane = origin.x + kLabelWidth;
    const float span = duration * mZoom;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Reserve the scrollable extent before drawing, so the horizontal scrollbar
    // matches the content rather than the visible part of it.
    ImGui::Dummy(ImVec2(kLabelWidth + span + 24.0f,
                        kRulerHeight + rows * kTrackHeight));

    const auto xOf = [&](float t) { return lane + t * mZoom; };
    const auto tOf = [&](float x) {
        return std::clamp((x - lane) / mZoom, 0.0f, duration);
    };

    // --- ruler -----------------------------------------------------------
    // A tick every 0.1 s, labelled every 0.5 s. Fixed rather than adaptive
    // because a clip is short by construction and an adaptive ruler that
    // relabels while you drag is harder to read than a dense one.
    draw->AddRectFilled(ImVec2(lane, origin.y),
                        ImVec2(lane + span, origin.y + kRulerHeight),
                        colour(ImGuiCol_FrameBg));
    for (int i = 0; i * 0.1f <= duration + 1e-4f; ++i) {
        const float t = float(i) * 0.1f;
        const bool major = (i % 5) == 0;
        const float x = xOf(t);
        draw->AddLine(ImVec2(x, origin.y + (major ? 4.0f : 12.0f)),
                      ImVec2(x, origin.y + kRulerHeight),
                      colour(ImGuiCol_TextDisabled, major ? 1.0f : 0.5f));
        // Majors continue faintly down through the track rows: reading whether
        // two keys on different tracks share a beat is the single thing a
        // timeline is looked at for, and it is guesswork without them.
        if (major) {
            draw->AddLine(
                ImVec2(x, origin.y + kRulerHeight),
                ImVec2(x, origin.y + kRulerHeight + rows * kTrackHeight),
                colour(ImGuiCol_Separator, 0.35f));
            char text[16];
            std::snprintf(text, sizeof(text), "%.1f", double(t));
            draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f),
                          colour(ImGuiCol_TextDisabled), text);
        }
    }

    // Clicking the ruler scrubs, which is where everyone tries first.
    ImGui::SetCursorScreenPos(ImVec2(lane, origin.y));
    ImGui::InvisibleButton("##scrub", ImVec2(std::max(span, 1.0f), kRulerHeight));
    if (ImGui::IsItemActive()) {
        clip.time = tOf(ImGui::GetIO().MousePos.x);
        clip.playing = false;
    }

    // --- tracks ----------------------------------------------------------
    int removeTrack = -1;
    for (std::size_t i = 0; i < clip.tracks.size(); ++i) {
        ClipTrack& track = clip.tracks[i];
        const float top = origin.y + kRulerHeight + float(i) * kTrackHeight;
        const float mid = top + kTrackHeight * 0.5f;

        if (i % 2)
            draw->AddRectFilled(ImVec2(origin.x, top),
                                ImVec2(lane + span, top + kTrackHeight),
                                colour(ImGuiCol_TableRowBgAlt));

        // The label: what this track drives, and where it is inert. A track
        // whose names did not resolve says so on the row rather than only in
        // the log, because the log has scrolled by the time anyone looks.
        char label[160];
        std::snprintf(label, sizeof(label), "%s%s.%s",
                      track.target.empty() ? "" : (track.target + "/").c_str(),
                      track.component.c_str(), track.field.c_str());
        const bool broken = track.resolved && track.typeIndex < 0;
        draw->AddText(ImVec2(origin.x + 6.0f, mid - 7.0f),
                      broken ? IM_COL32(230, 110, 110, 255)
                             : colour(ImGuiCol_Text),
                      label);

        draw->AddLine(ImVec2(lane, mid), ImVec2(lane + span, mid),
                      colour(ImGuiCol_Separator));

        // Per-row controls sit in the label gutter, over the drawn text.
        ImGui::SetCursorScreenPos(ImVec2(origin.x + kLabelWidth - 76.0f, top + 3.0f));
        ImGui::PushID(int(i));
        ImGui::SetNextItemWidth(50.0f);
        int ease = int(track.ease);
        if (ImGui::Combo("##ease", &ease, ecs::kClipEaseNames, ecs::kClipEaseCount))
            track.ease = ClipEase(ease);
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::SmallButton("x"))
            removeTrack = int(i);

        // --- keys --------------------------------------------------------
        // Still inside the row's PushID(i): the key ids below are pushed under
        // it, so key 0 of track 0 and key 0 of track 1 are different widgets.
        int removeKey = -1;
        for (std::size_t k = 0; k < track.keys.size(); ++k) {
            ClipKey& key = track.keys[k];
            const ImVec2 at(xOf(key.t), mid);
            const bool dragging = mDragTrack == int(i) && mDragKey == int(k);
            // The segment this key opens, so a track reads as spans of motion
            // rather than as unrelated dots. Drawn under the diamonds.
            if (k + 1 < track.keys.size())
                draw->AddLine(at, ImVec2(xOf(track.keys[k + 1].t), mid),
                              IM_COL32(140, 150, 170, 200), 2.5f);
            drawKeyDiamond(draw, at, dragging);

            // An invisible button per key, so ImGui owns the hit test and the
            // hover/active state rather than this file re-deriving them from
            // mouse position -- which is what makes two overlapping keys behave.
            ImGui::SetCursorScreenPos(ImVec2(at.x - kKeyRadius, at.y - kKeyRadius));
            // Nested under the row's own PushID(i) rather than a mixed
            // `i * 1024 + k`, which collides across tracks the moment one has
            // more than 1024 keys -- and an ImGui id collision is two widgets
            // sharing one active state, which reads as a possessed UI.
            ImGui::PushID(int(k));
            ImGui::InvisibleButton("##key", ImVec2(kKeyRadius * 2.0f, kKeyRadius * 2.0f));
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                mDragTrack = int(i);
                mDragKey = int(k);
                float t = tOf(ImGui::GetIO().MousePos.x);
                // Snap to the ruler's tenths unless Alt is held. A clip is a
                // handful of beats and "0.30 or 0.3001" is never the question
                // being asked; Alt is the escape hatch for the one time it is.
                if (!ImGui::GetIO().KeyAlt)
                    t = std::round(t * 10.0f) / 10.0f;
                key.t = std::clamp(t, 0.0f, duration);
                // Re-sorted on release, not here: sorting mid-drag renumbers
                // the key under the cursor and the drag jumps to a different one.
            }
            if (ImGui::IsItemDeactivated() && mDragTrack == int(i)) {
                sortKeys(track.keys);
                mDragTrack = mDragKey = -1;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("t = %.3f s\nvalue = %.3f, %.3f, %.3f\n"
                                  "right-click to delete",
                                  double(key.t), double(key.value.x),
                                  double(key.value.y), double(key.value.z));
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                removeKey = int(k);
            ImGui::PopID();
        }
        ImGui::PopID(); // the row's PushID(int(i))

        // Deferred: erasing inside the loop invalidates the indices the drag
        // state holds. The last key is kept -- a track with none is invisible
        // and unrecoverable, so "delete" would read as "the track vanished".
        if (removeKey >= 0 && track.keys.size() > 1) {
            track.keys.erase(track.keys.begin() + removeKey);
            mDragTrack = mDragKey = -1;
        }
    }
    if (removeTrack >= 0) {
        clip.tracks.erase(clip.tracks.begin() + removeTrack);
        // The drag indices name a track that no longer exists, and the value
        // editor below reads them. Bounds-checked there, so this is not a
        // crash -- it is worse: it would silently edit a different track.
        mDragTrack = mDragKey = -1;
    }

    // --- playhead --------------------------------------------------------
    // Drawn last so it is over the keys: it is the thing being read while
    // something plays, and a key on top of it hides exactly the moment of
    // interest.
    const float head = xOf(std::clamp(clip.time, 0.0f, duration));
    draw->AddLine(ImVec2(head, origin.y),
                  ImVec2(head, origin.y + kRulerHeight + rows * kTrackHeight),
                  IM_COL32(255, 90, 90, 230), 2.0f);

    ImGui::EndChild();

    // Adding a key: on every track, at the playhead, taking the value each
    // field has *right now*. That is the pose-then-key workflow every animator
    // expects, and it is only possible because the field is reachable
    // generically -- the panel reads it through the same registry the player
    // writes it through.
    if (!clip.tracks.empty()) {
        if (ImGui::Button("Key at playhead")) {
            for (ClipTrack& track : clip.tracks) {
                if (track.typeIndex < 0)
                    continue;
                const ComponentType& type =
                    mTypes->types()[std::size_t(track.typeIndex)];
                if (!type.instance)
                    continue;
                // The track's own target, not the clip's entity: a track aimed
                // at a child must be keyed from that child, or the key records
                // a value that looks right and is not.
                const entt::entity target = ecs::clipTrackTarget(
                    mWorld->registry(), mSelected, track.target);
                if (target == entt::null)
                    continue;
                void* instance = type.instance(mWorld->registry(), target);
                if (!instance)
                    continue;
                const eng::Field& field = type.fields[track.fieldIndex];
                const void* at = eng::fieldPtr(instance, field);
                ClipKey key;
                key.t = clip.time;
                switch (field.type) {
                case FieldType::Float:
                    key.value.x = *static_cast<const float*>(at);
                    break;
                case FieldType::Vec3:
                case FieldType::Colour:
                    key.value = *static_cast<const glm::vec3*>(at);
                    break;
                case FieldType::Bool:
                    key.value.x = *static_cast<const bool*>(at) ? 1.0f : 0.0f;
                    break;
                case FieldType::Int:
                    key.value.x = float(*static_cast<const int*>(at));
                    break;
                default:
                    continue;
                }
                track.keys.push_back(key);
                sortKeys(track.keys);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(drag a diamond to retime, right-click to delete)");
    }

    // The value editor for whichever key is under the cursor's last drag. Kept
    // out of the tooltip because a tooltip cannot be typed into.
    if (mDragTrack >= 0 && mDragTrack < int(clip.tracks.size())) {
        ClipTrack& track = clip.tracks[std::size_t(mDragTrack)];
        if (mDragKey >= 0 && mDragKey < int(track.keys.size()) &&
            track.typeIndex >= 0) {
            const ComponentType& type =
                mTypes->types()[std::size_t(track.typeIndex)];
            ClipKey& key = track.keys[std::size_t(mDragKey)];
            const int n = width(type.fields[track.fieldIndex].type);
            ImGui::SetNextItemWidth(220.0f);
            ImGui::DragScalarN("Key value", ImGuiDataType_Float, &key.value.x, n,
                               0.01f);
        }
    }
}

void ClipPanel::drawAddTrack()
{
    Clip& clip = mWorld->registry().get<Clip>(mSelected);
    const std::vector<ComponentType>& all = mTypes->types();

    ImGui::TextDisabled("Add track");

    // The component dropdown IS the registry, filtered to types that have at
    // least one interpolatable field. No hardcoded list, which is the whole
    // point: a component reflected tomorrow is animatable tomorrow.
    ImGui::SetNextItemWidth(180.0f);
    const char* typeLabel =
        (mPickType >= 0 && mPickType < int(all.size())) ? all[std::size_t(mPickType)].name
                                                        : "Component";
    if (ImGui::BeginCombo("##component", typeLabel)) {
        for (std::size_t i = 0; i < all.size(); ++i) {
            bool any = false;
            for (int f = 0; f < all[i].fieldCount && !any; ++f)
                any = animatable(all[i].fields[f].type);
            if (!any)
                continue;
            if (ImGui::Selectable(all[i].name, int(i) == mPickType)) {
                mPickType = int(i);
                mPickField = -1;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    const ComponentType* picked =
        (mPickType >= 0 && mPickType < int(all.size())) ? &all[std::size_t(mPickType)]
                                                        : nullptr;
    const char* fieldLabel =
        (picked && mPickField >= 0 && mPickField < picked->fieldCount)
            ? picked->fields[mPickField].name
            : "Field";
    if (ImGui::BeginCombo("##field", fieldLabel)) {
        if (picked) {
            for (int f = 0; f < picked->fieldCount; ++f) {
                if (!animatable(picked->fields[f].type))
                    continue;
                if (ImGui::Selectable(picked->fields[f].name, f == mPickField))
                    mPickField = f;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputTextWithHint("##target", "child (optional)", mPickTarget,
                             sizeof(mPickTarget));

    ImGui::SameLine();
    const bool ready = picked && mPickField >= 0;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Add") && ready) {
        ClipTrack track;
        track.target = mPickTarget;
        track.component = picked->name;
        track.field = picked->fields[mPickField].name;
        // Two keys, both at the field's current value: a track with one key is
        // a constant and a track with none is invisible, so the useful empty
        // state is "holds still for the whole clip, now move one end".
        ClipKey a;
        ClipKey b;
        b.t = clip.duration;
        track.keys = {a, b};
        clip.tracks.push_back(std::move(track));
    }
    ImGui::EndDisabled();
}

} // namespace eng
