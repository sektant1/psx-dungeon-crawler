#include <eng/StringId.h>

#include <eng/Log.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace eng {
namespace {

// The intern table. std::unordered_map is node-based, so a stored string's
// address survives a rehash -- which is what lets c_str() hand out a raw
// pointer that stays valid for the process lifetime.
struct Table
{
    std::mutex mutex;
    std::unordered_map<StringIdValue, std::string> entries;
    std::size_t collisions = 0;
};

Table& table()
{
    // Function-local static: interning can happen during static init (a
    // translation unit that hashes its literals into file-scope constants), and
    // a file-scope table would not be constructed yet.
    static Table t;
    return t;
}

} // namespace

StringId intern(std::string_view s)
{
    const StringId id(hashString(s));
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto [it, inserted] = t.entries.try_emplace(id.value(), s);
    if (!inserted && it->second != s) {
        // Two different names now mean the same thing to every system keyed by
        // id. Loud, because the failure downstream is silent: a lookup quietly
        // returns the other object.
        ++t.collisions;
        log::error("StringId: collision on 0x%016llx: '%s' vs '%s'",
                   static_cast<unsigned long long>(id.value()),
                   it->second.c_str(), std::string(s).c_str());
    }
    return id;
}

const char* stringFromId(StringId id)
{
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.entries.find(id.value());
    return it == t.entries.end() ? nullptr : it->second.c_str();
}

const char* StringId::c_str() const
{
    if (const char* s = stringFromId(*this))
        return s;
    // Never interned (a compile-time literal id, most likely). Print the number
    // rather than nothing: it still identifies the bin across a log or a panel.
    // thread_local so two threads formatting at once do not fight over it; the
    // buffer is only valid until this thread's next call, which matches how
    // every caller uses it (straight into a printf).
    static thread_local char buf[24];
    std::snprintf(buf, sizeof(buf), "#%016llx",
                  static_cast<unsigned long long>(mValue));
    return buf;
}

std::size_t internedCount()
{
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.mutex);
    return t.entries.size();
}

std::size_t stringIdCollisions()
{
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.mutex);
    return t.collisions;
}

} // namespace eng
