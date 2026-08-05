#pragma once

namespace eng::ecs {

// Makes a scene a **2D screen**: a menu, a HUD mock-up, a dialogue plate, an
// inventory page. Authored on the camera entity, exactly like its first- and
// third-person siblings, so "what kind of scene is this" is one component the
// author picks rather than a mode the game hard-codes.
//
// What it actually does is fix the camera square-on to the XY plane at the
// distance where `pageHeight` virtual pixels exactly fill the screen. Every
// entity in the scene is then authored in *pixels* -- a 32x32 icon is a 32x32
// quad, a panel at x = 24 is 24 pixels from the page's left edge -- and it
// stays that size at every window resolution.
//
// Why a camera and not a new renderer path: this project already has a
// pixel-exact immediate-mode UI (eng::ui::UiCanvas) for text, bars and anything
// the game computes per frame. What it did not have was a way to *author* a
// screen -- to place, nudge and preview a layout in the editor with the same
// tools that place a torch. A screen scene is that: the layout is entities in
// a .scn, and the canvas keeps drawing the live numbers over it.
//
// The one thing to know: the projection stays perspective, because changing it
// is changing the renderer, and the renderer's image is frozen. Anything on the
// page plane (z = 0) is pixel-exact; layers pushed off it by `layerSpacing` are
// scaled by perspective very slightly, which reads as depth and is why the
// spacing default is small.
struct ScreenCamera {
    // The design resolution, in virtual pixels. The height is what is
    // guaranteed: it always exactly fills the screen. The width is the page's
    // own extent, used to place things relative to its edges and to letterbox
    // when the window is wider than the page (see `fit`).
    float pageWidth = 428.0f;
    float pageHeight = 240.0f;

    // What happens when the window's aspect is not the page's.
    enum Fit : int {
        // The page height always fills the screen; a wider window simply sees
        // more of the world either side of the page. What a HUD wants -- the
        // vertical layout never moves.
        Height = 0,
        // The whole page is always visible: on a narrow window the camera
        // pulls back so the page's width fits, with empty space above and
        // below. What a menu or a dialogue plate wants -- nothing authored can
        // ever be cropped.
        Contain = 1,
    };
    // Held as int for the same reason Orbit::facing is: the reflection layer's
    // field types are the ones a byte stream and an ImGui widget both
    // understand.
    int fit = Height;

    // Distance between authored z layers, in world units. Entities are still
    // placed by their own transforms; this is what a game or an editor
    // multiplies a layer index by so that "background, panel, icon, tooltip"
    // is an integer rather than a hand-tuned depth.
    float layerSpacing = 0.5f;

    // Where the page's origin sits on screen. Centre is the natural one for a
    // menu (things stay centred at any aspect); TopLeft is the UI convention
    // and what a HUD authored against pixel coordinates wants.
    enum Origin : int { Centre = 0, TopLeft = 1 };
    int origin = Centre;

    // Vertical field of view the page is fitted into. Not a look choice here --
    // it only decides how far back the camera stands -- but a narrow one keeps
    // the perspective on off-plane layers subtle, which is the point.
    float fovDegrees = 40.0f;

    // Parked rather than deleted, like every other camera component.
    bool active = true;
};

} // namespace eng::ecs
