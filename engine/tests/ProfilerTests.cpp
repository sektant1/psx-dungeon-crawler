#include <eng/Profiler.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace eng;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ProfilerTests: " << m << '\n';
        std::exit(1);
    }
}

// Finds a node by name in the published tree. Returns -1 when absent.
static int find(const Profiler& p, const char* name)
{
    const auto& nodes = p.nodes();
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (std::strcmp(nodes[i].name, name) == 0)
            return int(i);
    return -1;
}

int main()
{
    // --- flat API ---------------------------------------------------------
    Profiler p;
    p.beginFrame();
    p.sample("physics", 2.0);
    p.sample("render", 5.0);
    p.sample("physics", 1.0); // same name accumulates within a frame
    p.endFrame();

    const auto& f = p.lastFrame();
    require(f.size() == 2, "two distinct entries");
    double physics = -1, render = -1;
    for (const auto& e : f) {
        if (e.name == "physics") physics = e.ms;
        if (e.name == "render")  render = e.ms;
    }
    require(physics == 3.0, "physics accumulated to 3ms");
    require(render == 5.0, "render is 5ms");

    p.beginFrame(); // new frame resets accumulation
    p.sample("physics", 0.5);
    p.endFrame();
    require(p.lastFrame().size() == 1, "new frame has one entry");
    require(p.lastFrame()[0].ms == 0.5, "new frame value fresh");

    // --- hierarchy --------------------------------------------------------
    // Timings measured by hand rather than by the wall clock, so the tree's
    // arithmetic is testable without sleeping.
    {
        Profiler h;
        h.beginFrame();
        h.push("render");
        h.sample("shadows", 4.0);
        h.sample("geometry", 6.0);
        h.pop();
        h.push("simulate");
        h.sample("physics", 2.0);
        h.pop();
        h.endFrame();

        const int rootIdx = 0;
        require(h.nodes()[rootIdx].depth == 0, "root is at depth 0");
        const int rnd = find(h, "render");
        const int sim = find(h, "simulate");
        const int sh = find(h, "shadows");
        require(rnd > 0 && sim > 0 && sh > 0, "scopes and samples are nodes");
        require(h.nodes()[std::size_t(sh)].parent == rnd,
                "a sample nests under the open scope");
        require(h.nodes()[std::size_t(sh)].depth == 2, "depth follows nesting");

        // A leaf's self time is its own time. A scope's is what is left after
        // its children -- the number that says whether to look here or one
        // level down. These children are hand-fed durations that no real work
        // backs, so the parent's measured wall time is smaller than their sum
        // and the subtraction clamps rather than printing a minus sign.
        require(h.selfMs(sh) == 4.0, "a leaf's self time is its own time");
        require(h.nodes()[std::size_t(sh)].ms == 4.0, "sample lands verbatim");
        require(h.selfMs(rnd) == 0.0, "self time never goes negative");

        // Pre-order: parents before their children, siblings in call order.
        const auto& order = h.preorder();
        require(order.size() == h.nodes().size(), "every node is visited once");
        require(order[0] == 0, "root comes first");
        int posRender = -1, posShadows = -1, posSim = -1;
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == rnd) posRender = int(i);
            if (order[i] == sh)  posShadows = int(i);
            if (order[i] == sim) posSim = int(i);
        }
        require(posRender < posShadows && posShadows < posSim,
                "children come before the next sibling");
    }

    // Re-entering a scope accumulates into one node and counts the calls: 2 ms
    // once and 2 ms across a thousand calls are different bugs.
    {
        Profiler h;
        h.beginFrame();
        for (int i = 0; i < 3; ++i) {
            h.push("ai");
            h.sample("pathfind", 1.0);
            h.pop();
        }
        h.endFrame();
        const int ai = find(h, "ai");
        const int path = find(h, "pathfind");
        require(ai > 0 && path > 0, "repeated scope is one node");
        require(h.nodes()[std::size_t(ai)].calls == 3, "calls are counted");
        require(h.nodes()[std::size_t(path)].ms == 3.0,
                "repeated samples accumulate into one node");
        require(int(h.nodes().size()) == 3, "no duplicate nodes per re-entry");
    }

    // An unbalanced frame (an early return inside a scope) must still publish a
    // complete tree rather than losing the frame.
    {
        Profiler h;
        h.beginFrame();
        h.push("outer");
        h.push("inner");
        h.endFrame(); // both left open
        require(find(h, "outer") > 0 && find(h, "inner") > 0,
                "unclosed scopes are still published");
        require(h.frameMs() >= 0.0, "root is closed by endFrame");

        // ...and an extra pop() must not corrupt the next frame.
        h.beginFrame();
        h.pop();
        h.pop();
        h.sample("after", 1.0);
        h.endFrame();
        const int after = find(h, "after");
        require(after > 0 && h.nodes()[std::size_t(after)].parent == 0,
                "an over-popped stack falls back to the root");
    }

    // RAII scope and the macro form measure real time and land in the tree.
    {
        Profiler h;
        h.beginFrame();
        {
            ENG_PROFILE(h, "block");
            volatile double sink = 0.0;
            for (int i = 0; i < 100000; ++i)
                sink += double(i);
            (void)sink;
        }
        h.endFrame();
        const int b = find(h, "block");
        require(b > 0, "ENG_PROFILE creates a node");
        require(h.nodes()[std::size_t(b)].calls == 1, "one call");
        require(h.nodes()[std::size_t(b)].ms > 0.0, "and measures real time");

        const std::string csv = h.toCsv();
        require(csv.rfind("depth,name,calls,inclusive_ms,self_ms", 0) == 0,
                "csv carries a header");
        require(csv.find("block") != std::string::npos, "csv lists the nodes");
    }

    std::cout << "ProfilerTests OK\n";
    return 0;
}
