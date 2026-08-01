#pragma once

#include <eng/Handles.h>
#include <eng/render/ModelImport.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eng::detail {

struct MeshGeometry {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
};

// Plain ownership bookkeeping for Renderer-created mesh resources. Handle
// slots never shift during a scene, so releasing one mesh cannot invalidate a
// different handle.
class MeshResources
{
public:
    MeshHandle add(std::string name, MeshGeometry geometry = {},
                   std::string importIdentity = {}, size_t submeshCount = 1,
                   ModelImportReport importReport = {})
    {
        mRecords.push_back({std::move(name), std::move(geometry),
                            std::move(importIdentity), submeshCount,
                            std::move(importReport)});
        return {static_cast<uint32_t>(mRecords.size())};
    }

    const std::string* name(MeshHandle handle) const
    {
        const Record* record = find(handle);
        return record ? &record->name : nullptr;
    }

    const MeshGeometry* geometry(MeshHandle handle) const
    {
        const Record* record = find(handle);
        return record ? &record->geometry : nullptr;
    }

    const std::string* importIdentity(MeshHandle handle) const
    {
        const Record* record = find(handle);
        return record ? &record->importIdentity : nullptr;
    }

    size_t submeshCount(MeshHandle handle) const
    {
        const Record* record = find(handle);
        return record ? record->submeshCount : 0;
    }

    const ModelImportReport* importReport(MeshHandle handle) const
    {
        const Record* record = find(handle);
        return record && record->importReport.succeeded()
                   ? &record->importReport
                   : nullptr;
    }

    bool markShared(MeshHandle handle)
    {
        Record* record = find(handle);
        if (!record)
            return false;
        record->shared = true;
        return true;
    }

    std::optional<std::string> release(MeshHandle handle)
    {
        Record* record = find(handle);
        if (!record || record->shared)
            return std::nullopt;
        std::string name = std::move(record->name);
        *record = {};
        return name;
    }

    std::vector<std::string> takeAll()
    {
        std::vector<std::string> names;
        names.reserve(mRecords.size());
        for (Record& record : mRecords)
            if (!record.name.empty())
                names.push_back(std::move(record.name));
        mRecords.clear();
        return names;
    }

private:
    struct Record {
        std::string name;
        MeshGeometry geometry;
        std::string importIdentity;
        size_t submeshCount = 1;
        ModelImportReport importReport;
        bool shared = false;
    };

    const Record* find(MeshHandle handle) const
    {
        if (!handle.valid() || handle.id > mRecords.size())
            return nullptr;
        const Record& record = mRecords[handle.id - 1];
        return record.name.empty() ? nullptr : &record;
    }

    Record* find(MeshHandle handle)
    {
        return const_cast<Record*>(
            std::as_const(*this).find(handle));
    }

    std::vector<Record> mRecords;
};

} // namespace eng::detail
