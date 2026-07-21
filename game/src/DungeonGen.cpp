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

    // Straight corridor cells orthogonally touching a room become arch
    // doorways. The tile asset is a complete straight tunnel, so never place
    // it at a bend or junction: its built-in side walls would otherwise seal
    // a live route or leave an unsupported edge. Collected from the label
    // grid (pre-arch), applied by the caller.
    std::vector<std::pair<int, int>> archCells() const {
        std::vector<std::pair<int, int>> out;
        for (int y = 1; y < kH - 1; ++y)
            for (int x = 1; x < kW - 1; ++x) {
                if (lab[size_t(y)][size_t(x)] != 'c') continue;
                const bool n = lab[size_t(y - 1)][size_t(x)] != '#';
                const bool s = lab[size_t(y + 1)][size_t(x)] != '#';
                const bool w = lab[size_t(y)][size_t(x - 1)] != '#';
                const bool e = lab[size_t(y)][size_t(x + 1)] != '#';
                const bool straight = (n && s && !w && !e) ||
                                      (w && e && !n && !s);
                const bool touchesRoom =
                    lab[size_t(y - 1)][size_t(x)] == 'r' ||
                    lab[size_t(y + 1)][size_t(x)] == 'r' ||
                    lab[size_t(y)][size_t(x - 1)] == 'r' ||
                    lab[size_t(y)][size_t(x + 1)] == 'r';
                if (straight && touchesRoom) out.push_back({x, y});
            }
        return out;
    }

    // Entry = leaf room centre nearest (0,0); anchor = farthest leaf from entry.
    void pickSetpieceLeaves(int& entry, int& anchor) const {
        entry = -1; anchor = -1;
        double bestNear = 1e18, bestFar = -1.0;
        int ecx = 0, ecy = 0;
        for (int i = 0; i < int(nodes.size()); ++i) {
            if (!nodes[size_t(i)].leaf) continue;
            const double cx = nodes[size_t(i)].rx + nodes[size_t(i)].rw / 2.0;
            const double cy = nodes[size_t(i)].ry + nodes[size_t(i)].rh / 2.0;
            const double d = cx * cx + cy * cy;
            if (d < bestNear) { bestNear = d; entry = i; ecx = int(cx); ecy = int(cy); }
        }
        for (int i = 0; i < int(nodes.size()); ++i) {
            if (!nodes[size_t(i)].leaf || i == entry) continue;
            const double cx = nodes[size_t(i)].rx + nodes[size_t(i)].rw / 2.0;
            const double cy = nodes[size_t(i)].ry + nodes[size_t(i)].rh / 2.0;
            const double d = (cx - ecx) * (cx - ecx) + (cy - ecy) * (cy - ecy);
            if (d > bestFar) { bestFar = d; anchor = i; }
        }
    }

    // Grow a leaf's room to >= 5x5 (clamped to leaf bounds), re-carving floor.
    void growRoom(int leaf) {
        Node& n = nodes[size_t(leaf)];
        const int rw = std::min(n.w - 2, std::max(5, n.rw));
        const int rh = std::min(n.h - 2, std::max(5, n.rh));
        n.rx = n.x + (n.w - rw) / 2;
        n.ry = n.y + (n.h - rh) / 2;
        n.rw = rw; n.rh = rh;
        for (int yy = n.ry; yy < n.ry + rh; ++yy)
            for (int xx = n.rx; xx < n.rx + rw; ++xx)
                lab[size_t(yy)][size_t(xx)] = 'r';
    }

    // One torch per room. DungeonMap mounts torches on walls, so the cell
    // must be a floor ('.') with an orthogonal solid '#' neighbour -- a
    // room-edge cell facing a doorway (arch/corridor) has none and would be
    // skipped at load. Collect every wall-adjacent floor cell in the room and
    // pick one via rng (deterministic, seeded); rooms ringed entirely by
    // doorways simply get no torch.
    void placeTorch(const Node& n, std::vector<std::string>& g) {
        std::vector<std::pair<int, int>> cand;
        for (int y = n.ry; y < n.ry + n.rh; ++y)
            for (int x = n.rx; x < n.rx + n.rw; ++x) {
                if (g[size_t(y)][size_t(x)] != '.') continue;
                if (g[size_t(y - 1)][size_t(x)] == '#' ||
                    g[size_t(y + 1)][size_t(x)] == '#' ||
                    g[size_t(y)][size_t(x - 1)] == '#' ||
                    g[size_t(y)][size_t(x + 1)] == '#')
                    cand.push_back({x, y});
            }
        if (cand.empty()) return;
        const auto [tx, ty] = cand[size_t(rnd(0, int(cand.size()) - 1))];
        g[size_t(ty)][size_t(tx)] = 'L';
    }

    // Flood over walkable cells in g from (sx,sy); returns reached count and
    // whether (tx,ty) was reached.
    int flood(const std::vector<std::string>& g, int sx, int sy,
              int tx, int ty, bool& reachedTarget) const {
        std::vector<std::string> seen(size_t(kH), std::string(size_t(kW), '0'));
        std::vector<std::pair<int, int>> st{{sx, sy}};
        seen[size_t(sy)][size_t(sx)] = '1';
        int count = 0;
        reachedTarget = false;
        while (!st.empty()) {
            auto [x, y] = st.back(); st.pop_back();
            ++count;
            if (x == tx && y == ty) reachedTarget = true;
            const std::pair<int, int> nb[4] = {
                {x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
            for (auto [nx, ny] : nb) {
                if (nx < 0 || ny < 0 || nx >= kW || ny >= kH) continue;
                if (seen[size_t(ny)][size_t(nx)] == '1') continue;
                const char cc = g[size_t(ny)][size_t(nx)];
                if (cc == '#' || cc == ' ') continue;
                seen[size_t(ny)][size_t(nx)] = '1';
                st.push_back({nx, ny});
            }
        }
        return count;
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
    for (uint32_t attempt = 0; attempt < 32; ++attempt) {
        Builder b(seed + attempt);
        b.nodes.push_back({1, 1, kW - 2, kH - 2});
        b.split(0, 0);
        const int leaves = b.leafCount();
        if (leaves < 6 || leaves > 10) continue; // want 6-10 rooms
        b.carveRooms();
        b.connect(0);

        int entry, anchor;
        b.pickSetpieceLeaves(entry, anchor);
        if (entry < 0 || anchor < 0) continue;
        b.growRoom(anchor); // ensure the demo scene fits (>= 5x5)

        // Floor/wall grid, then arches, torches, set-pieces.
        std::vector<std::string> g(size_t(kH), std::string(size_t(kW), '#'));
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (b.lab[size_t(y)][size_t(x)] != '#')
                    g[size_t(y)][size_t(x)] = '.';
        for (auto [x, y] : b.archCells())
            g[size_t(y)][size_t(x)] = 'A';
        for (const auto& n : b.nodes)
            if (n.leaf) b.placeTorch(n, g);

        const int scx = b.nodes[size_t(entry)].rx + b.nodes[size_t(entry)].rw / 2;
        const int scy = b.nodes[size_t(entry)].ry + b.nodes[size_t(entry)].rh / 2;
        const int ccx = b.nodes[size_t(anchor)].rx + b.nodes[size_t(anchor)].rw / 2;
        const int ccy = b.nodes[size_t(anchor)].ry + b.nodes[size_t(anchor)].rh / 2;
        g[size_t(scy)][size_t(scx)] = 'S';
        g[size_t(ccy)][size_t(ccx)] = 'C';

        // Down-portal 'X': a floor cell in the anchor room offset from 'C'
        // (the demo-scene centre). Prefer two cells up; else any other floor
        // cell in the room.
        {
            const Node& ar = b.nodes[size_t(anchor)];
            int px = ccx, py = ccy;
            if (ccy - 2 >= ar.ry && g[size_t(ccy - 2)][size_t(ccx)] == '.')
                py = ccy - 2;
            else {
                for (int yy = ar.ry; yy < ar.ry + ar.rh; ++yy)
                    for (int xxx = ar.rx; xxx < ar.rx + ar.rw; ++xxx)
                        if (g[size_t(yy)][size_t(xxx)] == '.') { px = xxx; py = yy; }
            }
            g[size_t(py)][size_t(px)] = 'X';
        }

        // Connectivity: from S reach C and every walkable cell.
        bool reachedC = false;
        const int reached = b.flood(g, scx, scy, ccx, ccy, reachedC);
        int walkable = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (g[size_t(y)][size_t(x)] != '#') ++walkable;
        if (reachedC && reached == walkable)
            return g;
        // else retry next seed
    }

    // Fallback: minimal valid grid (one S, one C, one A, connected) so the
    // game always boots even if 32 seeds somehow all fail.
    return {
        "#########",
        "#S.A.X.C#",
        "#########",
    };
}

} // namespace gen
