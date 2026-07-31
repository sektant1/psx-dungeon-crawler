#pragma once
#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::content {

// What a piece occupies and what may connect to it. Mirrors the vocabulary
// kit.toml documents; the strings live in the data, the meaning lives here.
enum class Socket {
    Floor,   // lies flat in a cell, occupies no volume
    Wall,    // stands on one cell edge, blocks movement across it
    Fill,    // occupies the whole cell volume (solid)
    Opening, // stands on a cell edge and is passable (arch, doorway)
    Prop,    // free-standing inside a cell, does not constrain the grid
};

const char* socketName(Socket socket);
bool socketFromName(std::string_view name, Socket& out);
// Grid-constrained sockets snap to a cell or an edge; Prop is placed freely.
inline bool socketUsesGrid(Socket socket) { return socket != Socket::Prop; }

// One entry of the modular kit. Sizes stay in *kit units* (pre-scale), exactly
// as authored, so this struct can be diffed against kit.toml without doing
// arithmetic; ask for metres explicitly.
struct KitPiece {
    std::string id;       // "kit.wall" -- with the prefix scenes reference
    std::string role;     // "wall", "floor_feature", "prop_light", ...
    std::string meshPath; // RELATIVE to the asset root: "meshes/kit/Wall_01.obj"
    std::string material; // "Game/Kit/Dungeon"
    Socket socket = Socket::Prop;

    glm::vec3 sizeKit{0.0f}; // straight off the mesh, kit units
    int span = 1;            // cells covered along its length
    // Non-zero only for the handful of pieces not authored centred on X/Z with
    // their base at Y=0 (a chandelier hangs from its mount, an arch is the head
    // of an opening). kit.toml calls these out with `pivot`.
    float yOffsetKit = 0.0f;
    std::string pivot; // "" = centred/base-at-zero, else the named exception

    glm::vec3 sizeMeters(float scale) const { return sizeKit * scale; }
    float yOffsetMeters(float scale) const { return yOffsetKit * scale; }
    // Local AABB in metres with the piece's own origin at (0,0,0), i.e. what a
    // placement ghost should draw and what picking should test. Applies the
    // base-at-Y=0 convention and the y_offset exception.
    void localBoundsMeters(float scale, glm::vec3& min, glm::vec3& max) const;
};

// kit.toml, parsed. This is the single reader of that file: the scene loader,
// the cooker and the editor all resolve prefab ids through it, so "what is
// kit.wall" has exactly one answer in the build.
class KitCatalog
{
public:
    static bool load(const std::string& tomlPath, KitCatalog& out,
                     std::string& error);

    float scale() const { return mScale; }             // 0.2 kit units -> m
    float cellSizeKit() const { return mCellSizeKit; } // 20
    float cellMeters() const { return mCellSizeKit * mScale; } // 4 m
    const std::string& meshDir() const { return mMeshDir; }

    // Null when the id is unknown -- that is the "unresolved prefab" the editor
    // must surface and the cooker must refuse.
    const KitPiece* find(std::string_view prefabId) const;
    const std::vector<KitPiece>& all() const { return mPieces; }
    // Both preserve kit.toml's order, which is authored to group by kind.
    std::vector<const KitPiece*> byRole(std::string_view role) const;
    std::vector<const KitPiece*> bySocket(Socket socket) const;
    std::vector<std::string> roles() const; // unique, in first-seen order

private:
    float mScale = 1.0f;
    float mCellSizeKit = 0.0f;
    std::string mMeshDir;
    std::vector<KitPiece> mPieces;
    std::unordered_map<std::string, std::size_t> mById;
};

} // namespace game::content
