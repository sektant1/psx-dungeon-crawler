#pragma once
#include "GameUiData.h"

#include <eng/ui/UiCanvas.h>

#include <entt/entt.hpp>

#include <string>
#include <vector>

namespace eng { class Input; }
namespace eng::ecs { class ComponentRegistry; }

// The authored screens, loaded and shown.
//
// Each screen is a cooked `.map` -- the same file a level is -- holding nothing
// but UI entities. They are loaded once at startup into registries of their own
// rather than merged into the level: a screen is not part of the world, must
// survive a level change, and must not appear in anything that iterates the
// scene looking for things to draw or simulate.
//
// Exactly one screen is open at a time. A stack was the obvious alternative and
// is the wrong shape here: the three screens are peers reached from the world
// (pack, shop, conversation), not layers over each other, and "escape closes
// the top one" is a rule that only earns its complexity when something opens
// over something else.
namespace game {

class UiScreens {
public:
    // Which screen, by the order they are registered. None is the world.
    enum class Screen { None, Inventory, Trade, Dialogue, Count };

    // Loads every screen. `types` must know the UI components; a screen that
    // fails to load is reported and left absent, and opening it then does
    // nothing rather than crashing -- a missing asset must not be fatal in a
    // shipped build.
    bool load(const eng::ecs::ComponentRegistry& types,
              const std::string& fontDefinition);

    void open(Screen screen);
    // Ends the conversation too, when one is showing: a dialogue screen closed
    // with the tree still running leaves the RPG layer believing the player is
    // mid-sentence, and the next NPC re-enters somebody else's conversation.
    void close();
    void toggle(Screen screen);
    Screen openScreen() const { return mOpen; }
    bool anyOpen() const { return mOpen != Screen::None; }

    // The data the open screen binds to. Exposed so the app can point it at the
    // trader being talked to before opening the shop.
    GameUiData& data() { return mData; }

    // The runtime every action acts on. Separate from `data()`'s copy because
    // the data source reads and this writes, and one pointer serving both would
    // make a const data source a lie.
    void setRuntime(rpg::RpgRuntime* rpg);

    // Reads the toggle bindings and drives the open screen's selection. Returns
    // true when it consumed the input, which is what stops a keypress that
    // closed a screen also firing a weapon.
    bool handleInput(eng::Input& input);

    // Pointer over the open screen. Hovering moves the cursor (and the focus,
    // when the pointer is over a different list); a click activates. Returns
    // true when the pointer was over the screen, which is what stops a click
    // meant for a shelf also swinging a weapon.
    //
    // `windowPixels` is where the pointer is; the canvas owns the mapping back
    // to virtual pixels.
    bool handleMouse(eng::Input& input, glm::vec2 windowPixels);

    // Run the open screen's action on the selected row. What that means is the
    // scene's to say: `UiList::action` names a verb and the table in the .cpp
    // maps it. Returns a line for the message feed, or empty when nothing
    // happened.
    std::string activate();

    // Draw the open screen over the whole window. A no-op when none is open.
    void draw(glm::vec2 displayPixels);

    // The last action's line, taken: reading it clears it, so one message
    // cannot be posted to the feed on every frame after the keypress.
    std::string takeMessage();

private:
    struct Loaded {
        entt::registry registry;
        bool ready = false;

        Loaded() = default;
        // Movable, never copied: entt::registry is move-only, and the vector
        // below has to be able to grow.
        Loaded(Loaded&&) = default;
        Loaded& operator=(Loaded&&) = default;
    };

    // Indexed by Screen. A vector rather than named members so adding a screen
    // is a row in the load table and nothing else.
    std::vector<Loaded> mScreens;
    Screen mOpen = Screen::None;
    GameUiData mData;
    eng::ui::UiCanvas mCanvas;
    bool mCanvasReady = false;
    // How many rows the open screen's list last reported, so selection can be
    // clamped without the input code knowing what a screen contains.
    int mRowCount = 0;
    // The open screen's actionable list, found on the last draw. Cached because
    // activate() must act on what the player is *looking at*, and re-deriving it
    // from the registry would pick the first list in scene order rather than the
    // one the cursor is in.
    // Every actionable list on the open screen, in authored order, and which
    // one has the cursor. A shop has two -- their shelf and your pack -- and
    // without this only the first could ever be acted on.
    struct ActionableList {
        std::string source;
        std::string action;
        int rows = 0;
    };
    std::vector<ActionableList> mActionable;
    int mFocus = 0;
    rpg::RpgRuntime* mRpg = nullptr;
    // The last solved layout, kept so a pointer can be hit-tested against what
    // was actually drawn rather than against a fresh solve that may disagree
    // with it by a frame.
    std::vector<eng::ui::UiSolvedRect> mSolved;
    // What the last action said, for the app to put in the message feed. Held
    // rather than returned from handleInput because the caller wants "did you
    // take this input" and the line is incidental to that answer.
    std::string mLastMessage;
};

} // namespace game
