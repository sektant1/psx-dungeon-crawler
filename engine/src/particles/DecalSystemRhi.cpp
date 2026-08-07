#include <eng/particles/DecalSystem.h>

#include <eng/particles/ParticleCollider.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>
#include <vector>

namespace eng {
namespace {

constexpr float kMergeNormalDot = 0.94f;
constexpr float kMergePlaneEpsilon = 0.06f;

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

struct DecalSystem::Impl {
    struct Decal {
        uint16_t profile = 0;
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec4 colour{1.0f};
        float size = 0.0f;
        float targetSize = 0.0f;
        float age = 0.0f;
        uint64_t sequence = 0;
    };

    std::vector<DecalProfileDesc> profiles;
    std::unordered_map<std::string, uint16_t> profileIds;
    std::vector<Decal> decals;
    size_t budget = 256;
    uint64_t nextSequence = 1;
    bool visible = true;
    std::mt19937 rng{0x5eed1234u};

    float random01()
    {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    }
    float randomSigned()
    {
        return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
    }

    void evictOldest()
    {
        if (decals.empty())
            return;
        const auto oldest = std::min_element(
            decals.begin(), decals.end(),
            [](const Decal& a, const Decal& b) {
                return a.sequence < b.sequence;
            });
        decals.erase(oldest);
    }

    void enforceBudget()
    {
        while (decals.size() > budget)
            evictOldest();
    }

    int findMergeTarget(uint16_t profile, glm::vec3 position,
                        glm::vec3 normal, float radius) const
    {
        const float radius2 = radius * radius;
        int best = -1;
        float bestDistance = radius2;
        for (size_t i = 0; i < decals.size(); ++i) {
            const Decal& decal = decals[i];
            if (decal.profile != profile ||
                glm::dot(decal.normal, normal) < kMergeNormalDot)
                continue;
            const glm::vec3 delta = position - decal.position;
            if (std::fabs(glm::dot(delta, normal)) > kMergePlaneEpsilon)
                continue;
            const float distance = glm::dot(delta, delta);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = int(i);
            }
        }
        return best;
    }
};

DecalSystem::DecalSystem() : mImpl(std::make_unique<Impl>()) {}
DecalSystem::~DecalSystem() = default;

void DecalSystem::shutdown() { clear(); }

void DecalSystem::registerProfile(const std::string& id,
                                  const DecalProfileDesc& desc)
{
    if (id.empty())
        return;
    if (const auto found = mImpl->profileIds.find(id);
        found != mImpl->profileIds.end()) {
        mImpl->profiles[found->second] = desc;
        return;
    }
    if (mImpl->profiles.size() >= 0xffffu)
        return;
    const uint16_t index = uint16_t(mImpl->profiles.size());
    mImpl->profiles.push_back(desc);
    mImpl->profileIds[id] = index;
}

const DecalProfileDesc* DecalSystem::profile(const std::string& id) const
{
    const auto found = mImpl->profileIds.find(id);
    return found == mImpl->profileIds.end()
               ? nullptr
               : &mImpl->profiles[found->second];
}

bool DecalSystem::spawn(const DecalRequest& request)
{
    const auto found = mImpl->profileIds.find(request.profile);
    if (found == mImpl->profileIds.end() || !finite(request.position) ||
        !finite(request.normal) ||
        glm::dot(request.normal, request.normal) < 1e-8f)
        return false;
    const uint16_t profileIndex = found->second;
    const DecalProfileDesc& desc = mImpl->profiles[profileIndex];
    const glm::vec3 normal = glm::normalize(request.normal);
    const float mergeRadius = std::clamp(desc.mergeRadius, 0.0f, 1.0f);
    if (desc.pool && mergeRadius > 0.0f) {
        const int target = mImpl->findMergeTarget(
            profileIndex, request.position, normal, mergeRadius);
        if (target >= 0) {
            Impl::Decal& decal = mImpl->decals[size_t(target)];
            decal.targetSize =
                std::min(std::max(desc.maxSize, 0.001f),
                         decal.targetSize + std::max(desc.sizeMin, 0.001f));
            decal.age = 0.0f;
            decal.sequence = mImpl->nextSequence++;
            return true;
        }
    }

    Impl::Decal decal;
    decal.profile = profileIndex;
    decal.position = request.position;
    decal.normal = normal;
    const float low = std::max(0.001f, std::min(desc.sizeMin, desc.sizeMax));
    const float high = std::max(low, std::max(desc.sizeMin, desc.sizeMax));
    decal.size = low + (high - low) * mImpl->random01();
    decal.targetSize = decal.size;
    decal.colour = desc.colour;
    decal.colour.r += desc.colourJitter.r * mImpl->randomSigned();
    decal.colour.g += desc.colourJitter.g * mImpl->randomSigned();
    decal.colour.b += desc.colourJitter.b * mImpl->randomSigned();
    decal.colour.a += desc.alphaJitter * mImpl->randomSigned();
    decal.colour = glm::max(decal.colour, glm::vec4(0.0f));
    decal.sequence = mImpl->nextSequence++;
    mImpl->decals.push_back(decal);
    mImpl->enforceBudget();
    return true;
}

void DecalSystem::update(float dt)
{
    dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
    for (size_t i = 0; i < mImpl->decals.size();) {
        Impl::Decal& decal = mImpl->decals[i];
        const DecalProfileDesc& desc = mImpl->profiles[decal.profile];
        decal.age += dt;
        if (desc.pool) {
            const float limit = std::max(0.001f, desc.maxSize);
            decal.targetSize = std::min(decal.targetSize, limit);
            decal.size = std::min(
                decal.targetSize,
                decal.size + std::max(0.0f, desc.growthRate) * dt);
        }
        if (desc.lifetime > 0.0f && decal.age >= desc.lifetime) {
            mImpl->decals.erase(mImpl->decals.begin() + ptrdiff_t(i));
        } else {
            ++i;
        }
    }
}

void DecalSystem::update(float dt, const glm::vec3&) { update(dt); }

void DecalSystem::clear() { mImpl->decals.clear(); }

void DecalSystem::setBudget(std::size_t maxDecals)
{
    mImpl->budget = std::max<size_t>(1, maxDecals);
    mImpl->enforceBudget();
}

std::size_t DecalSystem::budget() const { return mImpl->budget; }
std::size_t DecalSystem::count() const { return mImpl->decals.size(); }
void DecalSystem::setVisible(bool visible) { mImpl->visible = visible; }

} // namespace eng
