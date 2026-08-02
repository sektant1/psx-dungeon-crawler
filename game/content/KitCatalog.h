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

// Part attached to a prefab. Position uses the parent's authoring units; the
// cooker creates and parents the runtime entity automatically.
struct KitAttachment {
    std::string prefab;
    glm::vec3 position{0.0f};
};

// One entry of the modular kit. Sizes stay in *kit units* (pre-scale), exactly
// as authored, so this struct can be diffed against kit.toml without doing
// arithmetic; ask for metres explicitly.
struct KitPiece {
    std::string id;       // "kit.wall" -- with the prefix scenes reference
    std::string role;     // "wall", "floor_feature", "prop_light", ...
    std::string meshPath; // RELATIVE to the asset root: "meshes/kit/Wall_01.obj"
    std::string material; // "Game/Kit/Dungeon"
    Socket socket = Socket::Prop;

    // The scale this piece's mesh is imported at. Zero means "the kit's own",
    // which is what every architectural piece uses. Props authored in metres
    // (the meshes/props set) override it with 1.0, and then `sizeKit` below is
    // in metres too -- the sizes are always in the piece's own authoring units.
    float importScale = 0.0f;

    glm::vec3 sizeKit{0.0f}; // straight off the mesh, in authoring units
    int span = 1;            // cells covered along its length
    // Non-zero only for the handful of pieces not authored centred on X/Z with
    // their base at Y=0 (a chandelier hangs from its mount, an arch is the head
    // of an opening). kit.toml calls these out with `pivot`.
    float yOffsetKit = 0.0f;
    std::string pivot; // "" = centred/base-at-zero, else the named exception
    std::vector<KitAttachment> attachments;
    // Components an entity of this piece is meaningless without.
    //
    // The portal membrane is the case this exists for. Its material animates
    // from per-entity shader parameters, so a membrane with no `portal`
    // component is a flat violet rectangle -- and placing one from the Catalog
    // produced exactly that, while the gameplay "portal" entry produced a
    // working one. Two routes that look identical and are not is a trap, and
    // the fix belongs in the piece's own definition rather than in a special
    // case somewhere in the editor.
    //
    // Names index the editor's component registry (ed::findComponentType), so
    // adding one is a line of kit.toml, not a line of C++.
    std::vector<std::string> components;

    // `kitScale` is the catalogue's scale; a piece that carries its own wins.
    // Every caller passes catalog.scale() and gets the right answer either way,
    // which is what keeps a metre-authored prop from being shrunk to a fifth.
    float meshScale(float kitScale) const
    {
        return importScale > 0.0f ? importScale : kitScale;
    }
    glm::vec3 sizeMeters(float kitScale) const
    {
        return sizeKit * meshScale(kitScale);
    }
    float yOffsetMeters(float kitScale) const
    {
        return yOffsetKit * meshScale(kitScale);
    }
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
