#include <editor/assets/ResourceDbPanel.h>
#include <editor/content/SceneWorldExporter.h>

#include <eng/assets/AssetRoot.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ed {
namespace {

using eng::content::AssetType;
using eng::content::Record;
using eng::content::Setting;

// The colour vocabulary is the same one the Problems panel uses: green is
// nothing to do, amber is something you should do, grey is not applicable.
ImVec4 stateColour(int state)
{
    switch (state) {
    case 0: // Current
        return {0.45f, 0.80f, 0.45f, 1.0f};
    case 1: // Stale
        return {0.95f, 0.72f, 0.25f, 1.0f};
    case 2: // Unbuilt
        return {0.70f, 0.70f, 0.75f, 1.0f};
    default:
        return {0.55f, 0.55f, 0.58f, 1.0f};
    }
}

std::string humanBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer),
                  value < 10.0 && unit > 0 ? "%.1f %s" : "%.0f %s", value,
                  units[unit]);
    return buffer;
}

const char* stateName(ResourceDbPanel::Row::State state)
{
    switch (state) {
    case ResourceDbPanel::Row::State::Current:
        return "current";
    case ResourceDbPanel::Row::State::Stale:
        return "STALE";
    case ResourceDbPanel::Row::State::Unbuilt:
        return "not built";
    case ResourceDbPanel::Row::State::Skipped:
        return "skipped";
    case ResourceDbPanel::Row::State::NoExporter:
        return "no exporter";
    }
    return "?";
}

} // namespace

void ResourceDbPanel::refresh()
{
    if (mRegistry.all().empty()) {
        eng::acp::registerBuiltinExporters(mRegistry);
        mRegistry.add(makeSceneWorldExporter());
    }

    mDb.scan(eng::assets::root());

    std::string error;
    mPack.clear();
    if (eng::assets::cookedMounted())
        mPack.load(eng::assets::cookedDir(), error);

    rebuildRows();
    mScanned = true;
    mSelected = -1;
    mDirty = false;
}

void ResourceDbPanel::rebuildRows()
{
    mRows.clear();
    mRows.reserve(mDb.records().size());
    for (const Record& record : mDb.records()) {
        Row row;
        row.record = &record;

        if (eng::content::settingBool(record.import, "skip", false)) {
            row.state = Row::State::Skipped;
            mRows.push_back(row);
            continue;
        }

        const bool alreadyIntermediate =
            eng::content::assetStage(record.logical) ==
            eng::content::AssetStage::Intermediate;
        const eng::acp::Exporter* exporter =
            alreadyIntermediate ? &mRegistry.publisher()
                                : mRegistry.find(record.type);
        if (!exporter) {
            row.state = Row::State::NoExporter;
            mRows.push_back(row);
            continue;
        }

        row.output = exporter->outputFor(record);
        const eng::content::PackEntry* entry = mPack.bySource(record.logical);
        if (!entry) {
            row.state = Row::State::Unbuilt;
        } else {
            row.outputBytes = entry->outputBytes;
            // Same computation the pipeline plans with, so what the panel calls
            // stale is exactly what a build would rebuild -- a panel that
            // disagreed with the tool would be worse than no panel.
            std::vector<eng::content::Hash> dependencies;
            dependencies.reserve(entry->dependencies.size());
            for (const std::string& dependency : entry->dependencies) {
                if (const Record* tracked = mDb.findLogical(dependency))
                    dependencies.push_back(tracked->sourceHash);
                else
                    dependencies.push_back(
                        eng::content::hashFile(mDb.root() / dependency));
            }
            row.state = eng::acp::buildKey(record, *exporter, dependencies) ==
                                entry->buildKey
                            ? Row::State::Current
                            : Row::State::Stale;
        }
        mRows.push_back(row);
    }
}

void ResourceDbPanel::runBuild(const std::string& filter)
{
    eng::acp::PipelineOptions options;
    options.contentRoot = eng::assets::root();
    options.outputRoot = eng::assets::cookedMounted()
                             ? eng::assets::cookedDir()
                             : eng::assets::project() / "build" / "cooked";
    options.filter = filter;

    mBuilding = true;
    eng::acp::PipelineReport report;
    eng::acp::run(options, mRegistry, report);
    mBuilding = false;

    char summary[192];
    std::snprintf(summary, sizeof(summary),
                  "%zu built, %zu up to date, %zu failed in %.2fs",
                  report.built, report.upToDate, report.failed, report.seconds);
    mStatus = summary;

    mErrors.clear();
    for (const eng::acp::AssetOutcome& outcome : report.outcomes)
        if (outcome.state == eng::acp::AssetOutcome::State::Failed)
            mErrors.push_back(outcome.logical + ": " + outcome.message);

    // The pack the panel compares against has just changed underneath it, and
    // so has the engine's view of it.
    eng::assets::mountCooked(options.outputRoot.string());
    refresh();
}

void ResourceDbPanel::drawToolbar()
{
    if (ImGui::Button("Rescan"))
        mPending = Pending::Refresh;
    ImGui::SameLine();
    if (ImGui::Button("Build all")) {
        mPending = Pending::Build;
        mPendingFilter.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stamp missing .meta"))
        mPending = Pending::Stamp;

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::InputTextWithHint("##filter", "filter by path", mFilter,
                             sizeof(mFilter));
    ImGui::SameLine();

    std::string typeLabel =
        mTypeFilter < 0 ? "all types"
                        : std::string(eng::content::assetTypeName(
                              static_cast<AssetType>(mTypeFilter)));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (ImGui::BeginCombo("##type", typeLabel.c_str())) {
        if (ImGui::Selectable("all types", mTypeFilter < 0))
            mTypeFilter = -1;
        for (int i = 1; i < static_cast<int>(AssetType::Count); ++i) {
            const auto type = static_cast<AssetType>(i);
            const std::string name(eng::content::assetTypeName(type));
            if (ImGui::Selectable(name.c_str(), mTypeFilter == i))
                mTypeFilter = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("stale only", &mOnlyStale);

    size_t stale = 0, current = 0, unbuilt = 0;
    for (const Row& row : mRows) {
        if (row.state == Row::State::Stale)
            ++stale;
        else if (row.state == Row::State::Current)
            ++current;
        else if (row.state == Row::State::Unbuilt)
            ++unbuilt;
    }
    ImGui::TextDisabled("%zu assets  |  %zu current, %zu stale, %zu not built",
                        mRows.size(), current, stale, unbuilt);
    if (!mStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("--");
        ImGui::SameLine();
        ImGui::TextUnformatted(mStatus.c_str());
    }
    for (const std::string& error : mErrors)
        ImGui::TextColored({0.95f, 0.4f, 0.4f, 1.0f}, "%s", error.c_str());
}

void ResourceDbPanel::drawTable()
{
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    const float height = ImGui::GetContentRegionAvail().y * 0.6f;
    if (!ImGui::BeginTable("##resourcedb", 5, flags, {0.0f, height}))
        return;

    ImGui::TableSetupColumn("asset", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("output", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("meta", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(mRows.size()); ++i) {
        const Row& row = mRows[static_cast<size_t>(i)];
        if (mTypeFilter >= 0 &&
            static_cast<int>(row.record->type) != mTypeFilter)
            continue;
        if (mOnlyStale && row.state != Row::State::Stale)
            continue;
        if (mFilter[0] &&
            row.record->logical.find(mFilter) == std::string::npos)
            continue;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(row.record->logical.c_str(), mSelected == i,
                              ImGuiSelectableFlags_SpanAllColumns)) {
            mSelected = i;
            mDirty = false;
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(
            std::string(eng::content::assetTypeName(row.record->type)).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(stateColour(static_cast<int>(row.state)), "%s",
                           stateName(row.state));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.outputBytes
                                   ? humanBytes(row.outputBytes).c_str()
                                   : "--");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.record->hasSidecar ? "yes" : "--");
    }
    ImGui::EndTable();
}

void ResourceDbPanel::drawDetail()
{
    if (mSelected < 0 || mSelected >= static_cast<int>(mRows.size())) {
        ImGui::TextDisabled("Select an asset to see and edit its import "
                            "settings.");
        return;
    }
    const Row& row = mRows[static_cast<size_t>(mSelected)];
    Record* record = mDb.mutableRecord(row.record->guid);
    if (!record)
        return;

    ImGui::SeparatorText(record->logical.c_str());
    ImGui::Text("guid    %s", eng::content::hashToHex(record->guid).c_str());
    ImGui::Text("source  %s  (%s)", humanBytes(record->sourceBytes).c_str(),
                eng::content::hashToHex(record->sourceHash).c_str());
    if (!row.output.empty())
        ImGui::Text("output  %s", row.output.c_str());
    if (const eng::acp::Exporter* exporter =
            mRegistry.find(record->type))
        ImGui::Text("exporter %s", std::string(exporter->name()).c_str());

    ImGui::Spacing();
    ImGui::TextDisabled("import settings");

    // Typed widgets, because that is the whole reason Setting is a variant and
    // not a string bag: a bool gets a checkbox, and nobody has to remember
    // whether this file spells it "true" or "1".
    for (auto& [key, setting] : record->import) {
        ImGui::PushID(key.c_str());
        switch (setting.kind) {
        case Setting::Kind::Bool: {
            bool value = setting.boolean;
            if (ImGui::Checkbox(key.c_str(), &value)) {
                setting.boolean = value;
                mDirty = true;
            }
            break;
        }
        case Setting::Kind::Integer: {
            int value = static_cast<int>(setting.integer);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (ImGui::InputInt(key.c_str(), &value)) {
                setting.integer = value;
                mDirty = true;
            }
            break;
        }
        case Setting::Kind::Number: {
            auto value = static_cast<float>(setting.number);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (ImGui::InputFloat(key.c_str(), &value, 0.0f, 0.0f, "%.4f")) {
                setting.number = value;
                mDirty = true;
            }
            break;
        }
        case Setting::Kind::String: {
            char buffer[128] = {};
            std::snprintf(buffer, sizeof(buffer), "%s", setting.text.c_str());
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
            if (ImGui::InputText(key.c_str(), buffer, sizeof(buffer))) {
                setting.text = buffer;
                mDirty = true;
            }
            break;
        }
        case Setting::Kind::StringList:
            ImGui::LabelText(key.c_str(), "%zu entries", setting.list.size());
            break;
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!mDirty);
    if (ImGui::Button("Save .meta")) {
        std::string error;
        if (mDb.writeSidecar(*record, error)) {
            mStatus = "wrote " + record->logical + ".meta";
            mDirty = false;
            rebuildRows();
        } else {
            mStatus = "cannot write sidecar: " + error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Rebuild this asset")) {
        mPending = Pending::Build;
        mPendingFilter = record->logical;
    }
    if (mDirty) {
        ImGui::SameLine();
        ImGui::TextColored({0.95f, 0.72f, 0.25f, 1.0f},
                           "unsaved -- the pipeline reads the .meta, not this");
    }
}

void ResourceDbPanel::draw()
{
    if (!mScanned)
        refresh();
    drawToolbar();
    ImGui::Separator();
    drawTable();
    ImGui::Separator();
    drawDetail();

    // After every widget has been drawn, so nothing still holds a Row or a
    // Record when the vector behind them is replaced.
    switch (mPending) {
    case Pending::None:
        break;
    case Pending::Refresh:
        refresh();
        break;
    case Pending::Stamp: {
        eng::content::ResourceDb::ScanOptions options;
        options.stampMissing = true;
        mDb.scan(eng::assets::root(), options);
        mSelected = -1;
        mDirty = false;
        rebuildRows();
        mStatus = "wrote a sidecar for every asset that lacked one";
        break;
    }
    case Pending::Build:
        runBuild(mPendingFilter);
        break;
    }
    mPending = Pending::None;
    mPendingFilter.clear();
}

} // namespace ed
