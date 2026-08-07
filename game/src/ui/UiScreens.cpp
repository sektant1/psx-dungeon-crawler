#include "UiScreens.h"

#include "rpg/RpgRuntime.h"

#include <eng/Input.h>
#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/ecs/MapSerializer.h>
#include <eng/ecs/components/UiComponents.h>
#include <eng/ui/UiScene.h>

#include <algorithm>
#include <utility>

namespace game {
namespace {

// The screens, and the action that opens each. One table, so adding a screen is
// a row here plus the .scn -- which is the whole promise of authoring UI as
// scenes and would be broken by a switch statement per screen.
struct ScreenDef {
    UiScreens::Screen screen;
    const char* map;
    const char* action;
};

constexpr ScreenDef kScreens[] = {
    {UiScreens::Screen::Inventory, "scenes/ui/inventory.map", "inventory"},
    {UiScreens::Screen::Trade, "scenes/ui/trade.map", "trade"},
    // No binding: a conversation is opened by talking to somebody, not by a
    // key. It still closes on `ui_back` like the others.
    {UiScreens::Screen::Dialogue, "scenes/ui/dialogue.map", nullptr},
};

// The virtual resolution screens are authored against. The canvas picks the
// largest integer magnification that fits it, so this is a *minimum* rather
// than a fixed size -- a wider window gets a wider surface, and anchors are
// what make that mean something.
constexpr glm::ivec2 kAuthoredSize{320, 240};

} // namespace

bool UiScreens::load(const eng::ecs::ComponentRegistry& types,
                     const std::string& fontDefinition)
{
    // resize, not assign: a registry is not copyable, so assign() would need
    // a prototype to copy from and there is none.
    mScreens.clear();
    mScreens.resize(std::size_t(Screen::Count));
    mCanvasReady = mCanvas.initialise(fontDefinition);
    if (!mCanvasReady)
        eng::log::error("UI screens: font '%s' failed to load; screens are off",
                        fontDefinition.c_str());

    bool all = true;
    for (const ScreenDef& def : kScreens) {
        Loaded& slot = mScreens[std::size_t(def.screen)];
        const std::filesystem::path path = eng::assets::resolve(def.map);
        if (path.empty()) {
            eng::log::error("UI screens: '%s' is not in a mounted pack",
                            def.map);
            all = false;
            continue;
        }
        // A screen that fails to load leaves `ready` false and opening it does
        // nothing. Fatal would be wrong: a missing inventory screen is a bad
        // build, not a reason a player cannot play the level in front of them.
        slot.ready = eng::ecs::readMap(path.string(), slot.registry, types);
        if (!slot.ready) {
            eng::log::error("UI screens: '%s' failed to load", def.map);
            all = false;
        }
    }
    return all;
}

void UiScreens::close()
{
    if (mOpen == Screen::Dialogue && mRpg && mRpg->conversation().active())
        mRpg->endConversation();
    mOpen = Screen::None;
    mFocus = 0;
}

void UiScreens::open(Screen screen)
{
    if (screen == Screen::None) {
        close();
        return;
    }
    const std::size_t index = std::size_t(screen);
    if (index >= mScreens.size() || !mScreens[index].ready) {
        eng::log::warn("UI screens: screen %d is not loaded", int(screen));
        return;
    }
    mOpen = screen;
    mFocus = 0;
    // Selection is per-opening, not persistent: coming back to a shop with the
    // cursor on the row you bought from last time points at whatever moved into
    // that slot, which is a different item.
    mData.setSelectedRow(0);
}

void UiScreens::toggle(Screen screen)
{
    if (mOpen == screen)
        close();
    else
        open(screen);
}

bool UiScreens::handleInput(eng::Input& input)
{
    // Closing first, and unconditionally: a screen the player cannot get out of
    // is worse than one they cannot get into.
    if (mOpen != Screen::None && input.wasPressed("ui_back")) {
        close();
        return true;
    }

    for (const ScreenDef& def : kScreens) {
        if (!def.action || !input.wasPressed(def.action))
            continue;
        toggle(def.screen);
        return true;
    }

    if (mOpen == Screen::None)
        return false;

    // Left/right moves between the lists on a screen that has more than one.
    // Tab is deliberately not it: Tab already opens the pack, and a key that
    // means two things depending on what is on screen is a key nobody trusts.
    if (!mActionable.empty() &&
        (input.wasPressed("ui_left") || input.wasPressed("ui_right"))) {
        const int count = int(mActionable.size());
        mFocus = (mFocus + (input.wasPressed("ui_right") ? 1 : count - 1)) % count;
        mData.setSelectedRow(0);
        return true;
    }

    if (input.wasPressed("ui_accept")) {
        mLastMessage = activate();
        return true;
    }

    // Row movement. Clamped against what the last draw actually reported rather
    // than against the list's authored maxRows, so the cursor cannot run past
    // the end of a short inventory.
    bool used = false;
    int row = mData.selectedRow();
    if (input.wasPressed("ui_down")) { ++row; used = true; }
    if (input.wasPressed("ui_up"))   { --row; used = true; }
    const int rows = mActionable.empty()
                         ? mRowCount
                         : mActionable[std::size_t(std::clamp(
                                           mFocus, 0,
                                           int(mActionable.size()) - 1))]
                               .rows;
    if (used && rows > 0)
        mData.setSelectedRow((row % rows + rows) % rows);
    else if (used)
        mData.setSelectedRow(0);

    // An open screen swallows everything else. Movement and firing under an
    // open inventory is how a player dies while reading their pack.
    return true;
}

void UiScreens::setRuntime(rpg::RpgRuntime* rpg)
{
    mRpg = rpg;
    mData.setRuntime(rpg);
}

std::string UiScreens::activate()
{
    if (!mRpg || mActionable.empty() || mData.selectedRow() < 0)
        return {};
    const ActionableList& focused =
        mActionable[std::size_t(std::clamp(mFocus, 0,
                                           int(mActionable.size()) - 1))];
    const std::string& source = focused.source;
    const std::string& action = focused.action;

    // What the cursor is on, asked of the same data source that drew it. Going
    // back to the source rather than remembering the row's contents is what
    // keeps the action and the display from disagreeing: between the draw and
    // the keypress a pickup can have landed in the pack.
    std::vector<eng::ui::UiDataSource::Row> rows;
    mData.rows(source, mData.selectedRow() + 1, rows);
    if (mData.selectedRow() >= int(rows.size()))
        return {};

    // The item the row stands for. The Row carries a display label, which is
    // not an id -- so the id is resolved the same way the row was built, by
    // position in the container the source names.
    const auto stackAt = [&](const rpg::Container& from) -> const rpg::ItemStack* {
        const std::size_t index = std::size_t(mData.selectedRow());
        return index < from.stacks().size() ? &from.stacks()[index] : nullptr;
    };

    if (action == "reply") {
        // The conversation ends itself when a reply leads nowhere, so the
        // screen has to follow it rather than stay open on a dead tree.
        if (!mRpg->chooseReply(mData.selectedRow()))
            return {};
        if (!mRpg->conversation().active())
            close();
        mData.setSelectedRow(0);
        return {};
    }

    if (action == "seal") {
        const rpg::ItemStack* stack = stackAt(mRpg->inventory().backpack);
        if (!stack)
            return {};
        // Toggling, so one key both spends and releases a seal. Refusal is
        // reported: a seal silently not applied is the player believing
        // something is protected when it is not.
        const bool want = !stack->secured;
        const std::string item = stack->item;
        if (mRpg->setSecured(item, want))
            return want ? "Sealed." : "Seal released.";
        return want ? "No seals left, or that is already safe." : std::string();
    }

    if (action == "sell") {
        const rpg::ItemStack* stack = stackAt(mRpg->inventory().backpack);
        if (!stack)
            return {};
        const rpg::RpgRuntime::TradeResult result =
            mRpg->sell(mData.trader(), stack->item, 1);
        // The row under the cursor can have vanished -- selling the last of a
        // stack erases it -- so the cursor is pulled back rather than left
        // pointing past the end.
        mData.setSelectedRow(std::max(0, mData.selectedRow() - 1));
        return result.message;
    }

    if (action == "buy") {
        const rpg::TraderState* state = mRpg->market().state(mData.trader());
        if (!state)
            return {};
        // The shelf is a map, and the row order is its iteration order with the
        // empty entries skipped -- the same walk GameUiData does. Duplicated
        // deliberately rather than shared: the alternative is the data source
        // returning ids alongside labels, which would put the shop's identity
        // model into every list on every screen.
        int index = 0;
        for (const auto& [item, quantity] : state->stock) {
            if (quantity <= 0)
                continue;
            if (index == mData.selectedRow())
                return mRpg->buy(mData.trader(), item, 1).message;
            ++index;
        }
        return {};
    }

    if (action == "use") {
        const rpg::ItemStack* stack = stackAt(mRpg->inventory().backpack);
        if (!stack)
            return {};
        // Equipment equips, everything else is used. One key, because "which of
        // these two verbs does this row want" is a question the item already
        // answers and the player should not have to.
        const rpg::ItemLibrary::Ref def = mRpg->items().find(stack->item);
        if (def && def->slot != rpg::EquipSlot::None)
            return mRpg->equip(stack->item) ? "Equipped." : std::string();
        return {};
    }

    eng::log::warn("UI screens: no verb named '%s'", action.c_str());
    return {};
}

std::string UiScreens::takeMessage()
{
    return std::exchange(mLastMessage, std::string{});
}

bool UiScreens::handleMouse(eng::Input& input, glm::vec2 windowPixels)
{
    if (mOpen == Screen::None || !mCanvasReady || mSolved.empty())
        return false;
    const std::size_t index = std::size_t(mOpen);
    if (index >= mScreens.size() || !mScreens[index].ready)
        return false;

    const glm::ivec2 point = mCanvas.toVirtual(windowPixels);
    const eng::ui::UiListHit hit =
        eng::ui::pickUiRow(mScreens[index].registry, mSolved, point);
    if (hit.entity == entt::null)
        return false;

    const eng::ecs::UiList* list =
        mScreens[index].registry.try_get<eng::ecs::UiList>(hit.entity);
    if (!list)
        return false;

    // Focus follows the pointer, so a click on the other column does not need a
    // separate keypress to reach it first. Only actionable lists take focus: a
    // display-only list is not somewhere a cursor can usefully be.
    for (std::size_t i = 0; i < mActionable.size(); ++i) {
        if (mActionable[i].source != list->source)
            continue;
        const int rows = mActionable[i].rows;
        // Past the end of what the source produced: the geometry has a slot
        // there, the data does not, and selecting it would act on nothing.
        if (hit.row >= rows)
            return true;
        mFocus = int(i);
        mData.setSelectedRow(hit.row);
        if (input.wasMouseClicked())
            mLastMessage = activate();
        return true;
    }
    // Over the screen but not over anything actionable. Still consumed: a click
    // that falls through an open panel and fires a weapon is the worst bug this
    // whole layer can have.
    return true;
}

void UiScreens::draw(glm::vec2 displayPixels)
{
    if (mOpen == Screen::None || !mCanvasReady)
        return;
    const std::size_t index = std::size_t(mOpen);
    if (index >= mScreens.size() || !mScreens[index].ready)
        return;

    // Which list the cursor is in, so only that one draws a selected row. Set
    // before the paint: a screen with two lists would otherwise highlight the
    // same row index in both, and the player could not tell which one a
    // keypress would act on.
    mData.setFocusedSource(
        mActionable.empty()
            ? std::string{}
            : mActionable[std::size_t(std::clamp(
                              mFocus, 0, int(mActionable.size()) - 1))]
                  .source);

    mCanvas.begin(displayPixels, kAuthoredSize);
    // Solved once and kept: the pointer is tested against the layout that was
    // drawn, and paintUiScene takes the same list, so a click cannot land on a
    // box the player never saw.
    eng::ui::UiRect surface;
    surface.size = mCanvas.size();
    eng::ui::solveUiLayout(mScreens[index].registry, surface, mSolved);
    eng::ui::paintUiScene(mCanvas, mScreens[index].registry, mSolved, &mData);

    // How many rows the open screen offers, for the cursor clamp. Asked of the
    // data source rather than counted during paint: paint is the renderer's and
    // giving it an output parameter for the input code would tangle the two.
    mRowCount = 0;
    mActionable.clear();
    for (const auto& [entity, list] :
         mScreens[index].registry.view<const eng::ecs::UiList>().each()) {
        (void)entity;
        std::vector<eng::ui::UiDataSource::Row> rows;
        const int count =
            mData.rows(list.source, std::max(1, list.maxRows), rows);
        mRowCount = std::max(mRowCount, count);
        if (!list.action.empty())
            mActionable.push_back({list.source, list.action, count});
    }
    // A list can empty out while the cursor is in it -- selling the last of
    // something does exactly that -- so the focus is clamped here rather than
    // trusted to still be valid.
    if (!mActionable.empty())
        mFocus = std::clamp(mFocus, 0, int(mActionable.size()) - 1);
}

} // namespace game
