#pragma once
#include <eng/ui/UiScene.h>

#include <string>
#include <vector>

namespace game { namespace rpg { class RpgRuntime; } }

// What an authored screen's bindings resolve to.
//
// A UI scene names a key -- "inventory.weight_fraction", "trade.stock" -- and
// this answers it out of the live RPG runtime. That indirection is the whole
// reason a screen can be a scene: the .scn holds layout and placeholder text,
// this holds the numbers, and neither knows how the other is built.
//
// The keys are a **vocabulary**, so they live in one table (see the .cpp) rather
// than as string literals in a switch. Renaming one is an edit there and in the
// scenes that use it; adding one is a row.
//
// Unanswered keys are not an error. `UiDataSource` falls back to the authored
// value, which is what makes a screen previewable in an editor with no game
// behind it -- and what stops a typo in a binding from blanking a panel.
namespace game {

class GameUiData final : public eng::ui::UiDataSource {
public:
    // `rpg` must outlive this. Null is legal and answers nothing, which is the
    // headless and editor case.
    explicit GameUiData(rpg::RpgRuntime* rpg = nullptr) : mRpg(rpg) {}
    void setRuntime(rpg::RpgRuntime* rpg) { mRpg = rpg; }

    // Which trader's shelf `trade.*` reads, and which container the inventory
    // screen is acting on. Set by whoever opened the screen: a data source that
    // guessed would show the wrong shop.
    void setTrader(std::string trader) { mTrader = std::move(trader); }
    const std::string& trader() const { return mTrader; }
    // Row the player has highlighted, by index into the last list answered.
    // Presentation state, held here rather than in the scene because a scene
    // that stored it could not be reloaded without losing the player's place.
    void setSelectedRow(int row) { mSelected = row; }
    int selectedRow() const { return mSelected; }
    // Which list the cursor is in. Only that list marks a row selected: a
    // screen showing two lists would otherwise highlight the same index in
    // both, which tells the player nothing about where a keypress lands.
    void setFocusedSource(std::string source) { mFocused = std::move(source); }

    bool number(std::string_view key, float& out) const override;
    bool text(std::string_view key, std::string& out) const override;
    int rows(std::string_view key, int max,
             std::vector<Row>& out) const override;

    // Every key this source answers, for the editor's binding picker and for
    // the test that keeps the documentation honest.
    static std::vector<std::string> numberKeys();
    static std::vector<std::string> textKeys();
    static std::vector<std::string> rowKeys();

private:
    rpg::RpgRuntime* mRpg = nullptr;
    std::string mTrader;
    int mSelected = -1;
    std::string mFocused;
};

} // namespace game
