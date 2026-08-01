#pragma once

#include <functional>
#include <string>

namespace ed {

// "Are you sure?", once, for everything that asks it.
//
// The editor had exactly one confirmation -- the unsaved-work prompt -- written
// inline, with its own bool, its own popup id and its own three buttons. Every
// other destructive action either had no confirmation or would have grown a
// second copy of that code. This is the copy that does not have to be written
// again: a caller states the question and what to do about a yes.
//
// Deliberately modal and deliberately global. A confirmation that can be
// scrolled off screen, or that two callers can open at once, is a confirmation
// that gets clicked through.
class ConfirmDialog
{
public:
    // Opens the prompt. `detail` is the second line -- the specifics, which is
    // where the count and the names belong, so the question itself stays one
    // short sentence somebody will actually read.
    static void open(std::string question, std::string detail,
                     std::function<void()> onConfirm,
                     std::string confirmLabel = "Delete");

    // Draws it, if open. Call once per frame, inside the imgui frame. Returns
    // true on the frame the action ran.
    static bool draw();

    static bool isOpen();
    // Drops a pending prompt without running it. For the paths that tear the
    // document down underneath it -- the answer to a question about a scene
    // that no longer exists is not "yes".
    static void cancel();
};

} // namespace ed
