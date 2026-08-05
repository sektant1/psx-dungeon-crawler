#include <editor/scene/Attachments.h>

namespace ed {

using game::content::AuthorId;
using game::content::Entity;
using game::content::KitAttachment;
using game::content::KitCatalog;
using game::content::KitPiece;
using game::content::SceneDocument;

std::vector<Entity> buildAttachmentEntities(const KitCatalog& catalog,
                                            SceneDocument& ids,
                                            const Entity& root)
{
    std::vector<Entity> created;
    const KitPiece* piece = catalog.find(root.prefab);
    if (!piece || piece->attachments.empty())
        return created;

    // Copied out before the first push_back. `root` is routinely a reference
    // INTO ids.entities -- the placement path hands us the entity it has just
    // added -- and growing that vector would leave it dangling.
    const AuthorId rootId = root.id;
    const bool castShadows = root.castShadows;

    const auto emit = [&](auto&& self, const AuthorId& parentId,
                          const KitPiece& parentPiece) -> void {
        for (const KitAttachment& attachment : parentPiece.attachments) {
            const KitPiece* attached = catalog.find(attachment.prefab);
            if (!attached)
                continue; // KitCatalog::load rejects this first.
            Entity child;
            child.id = ids.allocateId(attachment.prefab);
            child.name = attached->id;
            child.prefab = attachment.prefab;
            child.parent = parentId;
            // The attachment's offset is already expressed in the parent's
            // frame, which is exactly what an authored child transform is --
            // so this is a copy, not a conversion.
            child.transform.position = attachment.position;
            child.castShadows = castShadows;
            // A part may be a compound piece itself. Its own attachments are
            // authored by the recursion below, so it has to carry the flag too
            // -- guarding only the root left every nested level expanded twice,
            // once from the document and once by the cooker.
            child.unpackedAttachments = !attached->attachments.empty();
            const AuthorId childId = child.id;
            created.push_back(child);
            ids.entities.push_back(std::move(child));
            self(self, childId, *attached);
        }
    };
    emit(emit, rootId, *piece);
    return created;
}

bool hasPackedAttachments(const KitCatalog& catalog, const Entity& entity)
{
    if (entity.unpackedAttachments)
        return false;
    const KitPiece* piece = catalog.find(entity.prefab);
    return piece && !piece->attachments.empty();
}

} // namespace ed
