#include "DungeonGen.h"

#include <algorithm>
#include <random>

namespace gen {
namespace {

constexpr int kW = 31, kH = 31; // grid dimensions (cells)
constexpr int kMinLeaf = 7;     // min leaf side that may split further
constexpr int kMinRoom = 3;     // min carved room side
constexpr int kMaxDepth = 4;    // BSP recursion cap (=> ~6-12 leaves)

struct Node {
    int x, y, w, h;               // region bounds
    int left = -1, right = -1;    // child indices, -1 = leaf
    int rx = 0, ry = 0, rw = 0, rh = 0; // carved room (leaves only)
    bool leaf = false;
};

struct Builder {
    std::mt19937 rng;
    std::vector<Node> nodes;
    // Label grid: '#' wall, 'r' room floor, 'c' corridor floor.
    std::vector<std::string> lab;

    explicit Builder(uint32_t seed)
        : rng(seed), lab(size_t(kH), std::string(size_t(kW), '#')) {}

    int rnd(int lo, int hi) { // inclusive
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    }

    // Index-safe: never hold a Node& across a push_back (vector may realloc).
    void split(int idx, int depth) {
        const int w = nodes[idx].w, h = nodes[idx].h;
        const int nx = nodes[idx].x, ny = nodes[idx].y;
        const bool canW = w >= 2 * kMinLeaf, canH = h >= 2 * kMinLeaf;
        if ((!canW && !canH) || depth >= kMaxDepth) {
            nodes[idx].leaf = true;
            return;
        }
        bool horizontal;
        if (canH && !canW) horizontal = true;
        else if (canW && !canH) horizontal = false;
        else horizontal = (h > w) ? true : (w > h ? false : (rnd(0, 1) == 1));
        int li, ri;
        if (horizontal) {
            const int cut = rnd(kMinLeaf, h - kMinLeaf);
            li = int(nodes.size()); nodes.push_back({nx, ny, w, cut});
            ri = int(nodes.size()); nodes.push_back({nx, ny + cut, w, h - cut});
        } else {
            const int cut = rnd(kMinLeaf, w - kMinLeaf);
            li = int(nodes.size()); nodes.push_back({nx, ny, cut, h});
            ri = int(nodes.size()); nodes.push_back({nx + cut, ny, w - cut, h});
        }
        nodes[idx].left = li;
        nodes[idx].right = ri;
        split(li, depth + 1);
        split(ri, depth + 1);
    }

    void carveRooms() {
        for (auto& n : nodes) {
            if (!n.leaf) continue;
            const int rw = rnd(kMinRoom, std::max(kMinRoom, n.w - 2));
            const int rh = rnd(kMinRoom, std::max(kMinRoom, n.h - 2));
            n.rw = rw; n.rh = rh;
            n.rx = n.x + rnd(1, std::max(1, n.w - rw - 1));
            n.ry = n.y + rnd(1, std::max(1, n.h - rh - 1));
            for (int yy = n.ry; yy < n.ry + rh; ++yy)
                for (int xx = n.rx; xx < n.rx + rw; ++xx)
                    lab[size_t(yy)][size_t(xx)] = 'r';
        }
    }

    // Representative floor cell of a subtree (leftmost leaf's room centre).
    void subtreeCentre(int idx, int& cx, int& cy) {
        while (!nodes[idx].leaf) idx = nodes[idx].left;
        cx = nodes[idx].rx + nodes[idx].rw / 2;
        cy = nodes[idx].ry + nodes[idx].rh / 2;
    }

    void carveH(int y, int x0, int x1) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
            if (lab[size_t(y)][size_t(x)] == '#')
                lab[size_t(y)][size_t(x)] = 'c'; // never overwrite room floor
    }
    void carveV(int x, int y0, int y1) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
            if (lab[size_t(y)][size_t(x)] == '#')
                lab[size_t(y)][size_t(x)] = 'c';
    }

    void connect(int idx) {
        if (nodes[idx].leaf) return;
        connect(nodes[idx].left);
        connect(nodes[idx].right);
        int ax, ay, bx, by;
        subtreeCentre(nodes[idx].left, ax, ay);
        subtreeCentre(nodes[idx].right, bx, by);
        if (rnd(0, 1)) { carveH(ay, ax, bx); carveV(bx, ay, by); }
        else          { carveV(ax, ay, by); carveH(by, ax, bx); }
    }

    std::vector<std::string> emitPlain() const {
        std::vector<std::string> g(size_t(kH), std::string(size_t(kW), '#'));
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (lab[size_t(y)][size_t(x)] != '#')
                    g[size_t(y)][size_t(x)] = '.';
        return g;
    }

    int leafCount() const {
        int n = 0;
        for (const auto& nd : nodes) if (nd.leaf) ++n;
        return n;
    }
};

} // namespace

std::vector<std::string> generate(uint32_t seed)
{
    Builder b(seed);
    b.nodes.push_back({1, 1, kW - 2, kH - 2}); // interior root (1-cell border)
    b.split(0, 0);
    b.carveRooms();
    b.connect(0);
    return b.emitPlain(); // Task 3 replaces this with arches + set-pieces
}

} // namespace gen
