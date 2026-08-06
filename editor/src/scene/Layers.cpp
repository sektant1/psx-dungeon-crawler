#include <editor/scene/Layers.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace ed::layers {
namespace {

using game::content::AuthorId;
using game::content::Entity;
using game::content::SceneDocument;

bool listed(const std::vector<std::string>& list, std::string_view id)
{
    for (const std::string& entry : list)
        if (entry == id)
            return true;
    return false;
}

void toggleIn(std::vector<std::string>& list, std::string_view id, bool on)
{
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i] != id)
            continue;
        if (!on)
            list.erase(list.begin() + std::ptrdiff_t(i));
        return;
    }
    if (on)
        list.emplace_back(id);
}

} // namespace

bool isHidden(const LayerSession& session, std::string_view layerId)
{
    if (session.soloing())
        return session.solo != layerId;
    return listed(session.hidden, layerId);
}

bool isLocked(const LayerSession& session, std::string_view layerId)
{
    return listed(session.locked, layerId);
}

void setHidden(LayerSession& session, std::string_view layerId, bool on)
{
    toggleIn(session.hidden, layerId, on);
}

void setLocked(LayerSession& session, std::string_view layerId, bool on)
{
    toggleIn(session.locked, layerId, on);
}

void toggleSolo(LayerSession& session, std::string_view layerId)
{
    session.solo = session.solo == layerId ? std::string() : std::string(layerId);
}

bool hidesEntity(const LayerSession& session, const Entity& entity)
{
    return isHidden(session, entity.layer);
}

bool locksEntity(const LayerSession& session, const Entity& entity)
{
    return isLocked(session, entity.layer);
}

std::vector<std::string> layerIds(const SceneDocument& document)
{
    std::vector<std::string> ids;
    ids.emplace_back(kDefaultLayer);
    for (const game::content::Layer& layer : document.layers)
        if (!layer.id.empty() && !listed(ids, layer.id))
            ids.push_back(layer.id);

    // Anything an entity names that no layer declares, in first-seen order.
    for (const Entity& entity : document.entities)
        if (!entity.layer.empty() && !listed(ids, entity.layer))
            ids.push_back(entity.layer);
    return ids;
}

std::string displayName(const SceneDocument& document, std::string_view layerId)
{
    if (layerId.empty())
        return kDefaultLayerName;
    if (const game::content::Layer* layer = document.findLayer(layerId))
        return layer->name.empty() ? layer->id : layer->name;
    return std::string(layerId) + " (missing)";
}

std::vector<LayerStat> stats(const SceneDocument& document)
{
    const std::vector<std::string> ids = layerIds(document);

    std::vector<LayerStat> rows;
    rows.reserve(ids.size());
    std::unordered_map<std::string, std::size_t> index;
    for (const std::string& id : ids) {
        LayerStat row;
        row.id = id;
        row.name = displayName(document, id);
        if (const game::content::Layer* layer = document.findLayer(id))
            row.colour = layer->colour;
        row.undeclared = !id.empty() && document.findLayer(id) == nullptr;
        index.emplace(id, rows.size());
        rows.push_back(std::move(row));
    }

    for (const Entity& entity : document.entities) {
        const auto found = index.find(entity.layer);
        if (found != index.end())
            ++rows[found->second].entities;
    }
    return rows;
}

std::vector<AuthorId> membersOf(const SceneDocument& document,
                                std::string_view layerId)
{
    std::vector<AuthorId> ids;
    for (const Entity& entity : document.entities)
        if (entity.layer == layerId)
            ids.push_back(entity.id);
    return ids;
}

SceneDocument extractLayer(const SceneDocument& document,
                           std::string_view layerId)
{
    SceneDocument out;
    out.id = document.id;
    out.palette = document.palette;
    if (const game::content::Layer* layer = document.findLayer(layerId))
        out.layers.push_back(*layer);

    std::unordered_set<std::string> taken;
    for (const Entity& entity : document.entities)
        if (entity.layer == layerId)
            taken.insert(entity.id);

    // Every ancestor of a member that is not itself a member. Emitted as a bare
    // transform node: the extracted children's local transforms are expressed
    // in its frame, and without it a lighting pass opened on its own would put
    // every torch at the world origin.
    std::vector<AuthorId> stubs;
    std::unordered_set<std::string> stubbed;
    for (const Entity& entity : document.entities) {
        if (entity.layer != layerId)
            continue;
        std::unordered_set<std::string> seen;
        for (AuthorId at = entity.parent; !at.empty();) {
            if (!seen.insert(at).second)
                break; // a cycle; validate() reports it, this must not hang
            if (taken.count(at) == 0 && stubbed.insert(at).second)
                stubs.push_back(at);
            const Entity* parent = document.find(at);
            at = parent ? parent->parent : AuthorId();
        }
    }

    // Document order throughout, so an extract and a re-merge do not reshuffle
    // a file somebody is reviewing as a diff.
    for (const Entity& entity : document.entities) {
        if (stubbed.count(entity.id) != 0) {
            Entity stub;
            stub.id = entity.id;
            stub.name = entity.name;
            stub.parent = entity.parent;
            stub.transform = entity.transform;
            stub.layer = entity.layer;
            out.add(std::move(stub));
        } else if (entity.layer == layerId) {
            out.add(entity);
        }
    }
    return out;
}

MergeReport mergeLayer(SceneDocument& document, const SceneDocument& incoming,
                       std::string_view layerId)
{
    MergeReport report;

    // An incoming entity NOT in the layer being merged is an ancestor stub
    // extractLayer emitted so the members' transforms would resolve. One that
    // the document already has is the same entity, not a collision: re-adding
    // it would give the level two copies of one wall, and renaming it would
    // detach the children that point at it.
    std::unordered_set<std::string> reuse;
    for (const Entity& entity : incoming.entities)
        if (entity.layer != layerId && document.contains(entity.id))
            reuse.insert(entity.id);

    // Rename map built over everything else first, so a parent link can be
    // rewritten whether its target was renamed before or after it.
    std::unordered_map<std::string, std::string> renamed;
    std::unordered_set<std::string> claimed;
    for (const Entity& entity : incoming.entities) {
        if (reuse.count(entity.id) != 0)
            continue;
        if (!document.contains(entity.id) && claimed.count(entity.id) == 0) {
            claimed.insert(entity.id);
            continue;
        }
        // allocateId() only knows about the document, so ids claimed earlier in
        // this same merge have to be excluded by hand -- otherwise two colliding
        // entities are both offered the same free name.
        std::string candidate = document.allocateId(entity.id);
        for (int guard = 0; claimed.count(candidate) != 0 && guard < 100000;
             ++guard) {
            candidate = document.allocateId(candidate);
        }
        claimed.insert(candidate);
        renamed.emplace(entity.id, candidate);
        report.renamed.emplace_back(entity.id, candidate);
    }

    const auto resolve = [&renamed](const AuthorId& id) {
        const auto found = renamed.find(id);
        return found == renamed.end() ? id : found->second;
    };

    for (const Entity& source : incoming.entities) {
        if (reuse.count(source.id) != 0) {
            ++report.skipped;
            continue;
        }

        Entity entity = source;
        entity.id = resolve(source.id);
        entity.parent = resolve(source.parent);
        // Members land in the target layer; a stub keeps whatever layer it came
        // from, because it is scaffolding rather than part of the delivery.
        if (source.layer == layerId)
            entity.layer = std::string(layerId);
        document.add(std::move(entity));
        ++report.added;
    }

    if (!layerId.empty() && !document.findLayer(layerId)) {
        game::content::Layer layer;
        layer.id = std::string(layerId);
        if (const game::content::Layer* source = incoming.findLayer(layerId)) {
            layer.name = source->name;
            layer.colour = source->colour;
        } else {
            layer.name = layer.id;
        }
        document.layers.push_back(std::move(layer));
    }
    document.touch();
    return report;
}

} // namespace ed::layers
