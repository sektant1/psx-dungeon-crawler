#include <editor/content/KitCatalog.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game::content {
namespace {

struct SocketName {
    Socket socket;
    const char* name;
};
constexpr SocketName kSockets[] = {
    {Socket::Floor, "floor"},     {Socket::Wall, "wall"},
    {Socket::Fill, "fill"},       {Socket::Opening, "opening"},
    {Socket::Prop, "prop"},
};

} // namespace

const char* socketName(Socket socket)
{
    for (const SocketName& entry : kSockets)
        if (entry.socket == socket) return entry.name;
    return "prop";
}

bool socketFromName(std::string_view name, Socket& out)
{
    for (const SocketName& entry : kSockets) {
        if (name == entry.name) {
            out = entry.socket;
            return true;
        }
    }
    return false;
}

void KitPiece::localBoundsMeters(float scale, glm::vec3& min,
                                 glm::vec3& max) const
{
    const glm::vec3 size = sizeMeters(scale);
    const float y = yOffsetMeters(scale);
    // X/Z centred on the origin; Y sits on the base by convention, and y_offset
    // shifts it for the pieces authored around their centre or their mount.
    min = {-size.x * 0.5f, y, -size.z * 0.5f};
    max = {size.x * 0.5f, y + size.y, size.z * 0.5f};
}

bool KitCatalog::load(const std::string& tomlPath, KitCatalog& out,
                      std::string& error)
{
    error.clear();
    const toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        error = tomlPath + ": " + std::string(parsed.error().description());
        return false;
    }
    const toml::table* kit = parsed.table()["kit"].as_table();
    const toml::array* pieces = parsed.table()["piece"].as_array();
    if (!kit || !pieces) {
        error = tomlPath + ": missing [kit] or [[piece]]";
        return false;
    }

    KitCatalog catalog;
    catalog.mScale = float((*kit)["scale"].value_or(0.0));
    catalog.mCellSizeKit = float((*kit)["cell_size"].value_or(0.0));
    catalog.mMeshDir = (*kit)["mesh_dir"].value_or(std::string());
    if (!(catalog.mScale > 0.0f) || !(catalog.mCellSizeKit > 0.0f) ||
        catalog.mMeshDir.empty()) {
        error = tomlPath + ": kit scale, cell_size and mesh_dir are required";
        return false;
    }

    catalog.mPieces.reserve(pieces->size());
    for (const toml::node& node : *pieces) {
        const toml::table* piece = node.as_table();
        if (!piece)
            continue;
        KitPiece entry;
        const std::string id = (*piece)["id"].value_or(std::string());
        const std::string mesh = (*piece)["mesh"].value_or(std::string());
        entry.material = (*piece)["material"].value_or(std::string());
        entry.role = (*piece)["role"].value_or(std::string());
        // A piece with parts and no mesh of its own is a GROUP: the root of a
        // multi-part model, whose whole geometry lives in its attachments. The
        // importer writes one for every source model that arrives as more than
        // one submesh, because otherwise a twenty-four-part model is
        // twenty-four unrelated placeables and no way to place the model.
        const bool group =
            mesh.empty() && (*piece)["attachments"].as_array() != nullptr;
        if (id.empty() || (!group && (mesh.empty() || entry.material.empty()))) {
            error = tomlPath + ": piece '" + id +
                    "' needs id, mesh and material (or attachments, for a "
                    "mesh-less group)";
            return false;
        }
        entry.id = "kit." + id;
        // A bare filename lives in the kit's mesh dir; a path with a separator
        // is pack-relative, which is how the prop and set-dressing meshes join
        // the catalogue without a second one. A group has none, and every
        // consumer of meshPath already asks whether it is empty.
        entry.meshPath = mesh.empty() ? std::string()
                         : mesh.find('/') == std::string::npos
                             ? catalog.mMeshDir + "/" + mesh
                             : mesh;
        entry.importScale = float((*piece)["import_scale"].value_or(0.0));

        const std::string socket =
            (*piece)["socket"].value_or(std::string("prop"));
        if (!socketFromName(socket, entry.socket)) {
            error = tomlPath + ": piece '" + id + "' has unknown socket '" +
                    socket + "'";
            return false;
        }

        if (const toml::array* size = (*piece)["size"].as_array()) {
            if (size->size() != 3) {
                error = tomlPath + ": piece '" + id + "' size must be [x,y,z]";
                return false;
            }
            entry.sizeKit = {float((*size)[0].value_or(0.0)),
                             float((*size)[1].value_or(0.0)),
                             float((*size)[2].value_or(0.0))};
        }
        entry.span = int((*piece)["span"].value_or(1));
        entry.yOffsetKit = float((*piece)["y_offset"].value_or(0.0));
        entry.pivot = (*piece)["pivot"].value_or(std::string());
        if (const toml::array* components = (*piece)["components"].as_array()) {
            for (const toml::node& componentNode : *components) {
                const std::optional<std::string> name =
                    componentNode.value<std::string>();
                if (!name || name->empty()) {
                    error = tomlPath + ": piece '" + id +
                            "' components must be component-type names";
                    return false;
                }
                entry.components.push_back(*name);
            }
        }
        if (const toml::array* attachments = (*piece)["attachments"].as_array()) {
            for (const toml::node& attachmentNode : *attachments) {
                const toml::table* attachment = attachmentNode.as_table();
                if (!attachment) {
                    error = tomlPath + ": piece '" + id +
                            "' attachments must be inline tables";
                    return false;
                }
                KitAttachment part;
                part.prefab =
                    (*attachment)["prefab"].value_or(std::string());
                const toml::array* position =
                    (*attachment)["position"].as_array();
                if (part.prefab.empty() || !position || position->size() != 3) {
                    error = tomlPath + ": piece '" + id +
                            "' attachment needs prefab and position [x,y,z]";
                    return false;
                }
                part.position = {float((*position)[0].value_or(0.0)),
                                 float((*position)[1].value_or(0.0)),
                                 float((*position)[2].value_or(0.0))};
                entry.attachments.push_back(std::move(part));
            }
        }
        if (entry.span < 1) {
            error = tomlPath + ": piece '" + id + "' span must be >= 1";
            return false;
        }

        if (!catalog.mById.emplace(entry.id, catalog.mPieces.size()).second) {
            error = tomlPath + ": duplicate kit piece '" + id + "'";
            return false;
        }
        catalog.mPieces.push_back(std::move(entry));
    }
    if (catalog.mPieces.empty()) {
        error = tomlPath + ": no [[piece]] entries";
        return false;
    }
    for (const KitPiece& piece : catalog.mPieces) {
        for (const KitAttachment& attachment : piece.attachments) {
            if (!catalog.find(attachment.prefab)) {
                error = tomlPath + ": piece '" + piece.id +
                        "' has unresolved attachment '" + attachment.prefab + "'";
                return false;
            }
        }
    }
    std::vector<unsigned char> attachmentState(catalog.mPieces.size(), 0);
    const auto visitAttachments = [&](auto&& self, std::size_t index) -> bool {
        if (attachmentState[index] == 1) {
            error = tomlPath + ": attachment cycle at piece '" +
                    catalog.mPieces[index].id + "'";
            return false;
        }
        if (attachmentState[index] == 2)
            return true;
        attachmentState[index] = 1;
        for (const KitAttachment& attachment :
             catalog.mPieces[index].attachments) {
            const auto child = catalog.mById.find(attachment.prefab);
            if (!self(self, child->second))
                return false;
        }
        attachmentState[index] = 2;
        return true;
    };
    for (std::size_t index = 0; index < catalog.mPieces.size(); ++index)
        if (!visitAttachments(visitAttachments, index))
            return false;

    out = std::move(catalog);
    return true;
}

const KitPiece* KitCatalog::find(std::string_view prefabId) const
{
    const auto found = mById.find(std::string(prefabId));
    return found == mById.end() ? nullptr : &mPieces[found->second];
}

std::vector<const KitPiece*> KitCatalog::byRole(std::string_view role) const
{
    std::vector<const KitPiece*> result;
    for (const KitPiece& piece : mPieces)
        if (piece.role == role) result.push_back(&piece);
    return result;
}

std::vector<const KitPiece*> KitCatalog::bySocket(Socket socket) const
{
    std::vector<const KitPiece*> result;
    for (const KitPiece& piece : mPieces)
        if (piece.socket == socket) result.push_back(&piece);
    return result;
}

std::vector<std::string> KitCatalog::roles() const
{
    std::vector<std::string> result;
    for (const KitPiece& piece : mPieces) {
        if (piece.role.empty())
            continue;
        if (std::find(result.begin(), result.end(), piece.role) == result.end())
            result.push_back(piece.role);
    }
    return result;
}

} // namespace game::content
