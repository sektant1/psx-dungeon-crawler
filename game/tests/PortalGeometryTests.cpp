// The portal's depth ordering, which is the one thing about this prop that has
// gone wrong repeatedly: the glowing membrane showing past the stone that is
// meant to hide it. Every failure so far has been arithmetic -- a surface at
// the wrong Z -- so it is checked here rather than by eye in a screenshot.
#include "SceneFactory.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "PortalGeometryTests: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(float actual, float expected, float tolerance,
                 const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        std::fprintf(stderr,
                     "PortalGeometryTests: %s (expected %.4f, got %.4f)\n",
                     message, expected, actual);
        std::exit(1);
    }
}

} // namespace

int main()
{
    // --- the shipped defaults ------------------------------------------------
    {
        const PortalPropStyle style;
        const PortalDepths d = portalDepths(style);

        requireNear(d.membraneBack,
                    style.membraneInset - style.membraneThickness * 0.5f,
                    1e-5f, "membrane back face");
        requireNear(d.membraneFront,
                    style.membraneInset + style.membraneThickness * 0.5f,
                    1e-5f, "membrane front face");

        // The bug this file exists for: the backing used to be a zero-thickness
        // sheet parked behind the pillars, leaving a third of a metre of open
        // slot between it and the membrane. The kit Pillar is a round shaft
        // (radius about 0.71 of its bounding box), so it screens neither the
        // corners of its footprint nor that slot, and an oblique view looked
        // straight through into the membrane's lit edge.
        require(d.backingGap() > 0.0f,
                "backing must not touch the membrane -- coplanar faces z-fight");
        require(d.backingGap() < 0.05f,
                "backing must sit right behind the membrane; any wider is an "
                "open slot the round pillars cannot screen");

        // It still has to clear the pillars at the back, or it reads as a panel
        // jammed between the posts rather than a wall closing the surround.
        require(d.panelBack < d.pillarBack,
                "backing must pass behind the pillars' rear faces");

        // And it must be a slab, not a sheet.
        require(d.panelFront - d.panelBack > 0.1f,
                "backing has no meaningful depth");
    }

    // --- the invariants hold across tuning -----------------------------------
    // These are the knobs a level author actually turns, and the ordering must
    // survive all of them rather than being true only for the shipped numbers.
    for (const float inset : {0.0f, 0.02f, 0.06f, 0.2f, 0.5f}) {
        for (const float thickness : {0.02f, 0.14f, 0.4f}) {
            for (const float pillar : {0.3f, 0.5f, 0.9f}) {
                PortalPropStyle style;
                style.membraneInset = inset;
                style.membraneThickness = thickness;
                style.framePillarWidth = pillar;
                const PortalDepths d = portalDepths(style);

                require(d.panelFront > d.panelBack,
                        "backing depth went negative under tuning");
                require(d.panelBack < d.pillarBack,
                        "backing stopped clearing the pillars under tuning");
                require(d.membraneFront > d.membraneBack,
                        "membrane depth went negative under tuning");
                // The panel may end up in front of the membrane's back face
                // only when the membrane is pushed so far back that it would be
                // inside the wall anyway; what must never happen is the panel
                // falling far behind it and reopening the slot.
                require(d.backingGap() < 0.05f + 1e-5f,
                        "tuning reopened the slot between backing and "
                        "membrane");
            }
        }
    }

    // A membrane pushed hard against the pillars must not force the backing
    // into a degenerate or inverted slab; the floor on its depth takes over.
    {
        PortalPropStyle style;
        style.membraneInset = -0.5f; // behind the frame plane entirely
        const PortalDepths d = portalDepths(style);
        require(d.panelFront - d.panelBack >= 0.05f - 1e-5f,
                "backing collapsed when the membrane was pushed behind the "
                "frame");
    }

    std::cout << "PortalGeometryTests OK\n";
}
