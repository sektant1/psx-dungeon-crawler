#include <eng/TextResource.h>
#include <eng/ResourceCache.h>
#include <eng/Content.h>
#include <eng/FileSystem.h>
#include <cctype>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"ContentTests: "<<m<<'\n'; std::exit(1);} }

struct UpperResource : eng::Resource {
    UpperResource(std::string n, std::string p) : Resource(std::move(n), std::move(p)) {}
    std::string value;
    bool load() override {
        std::string s;
        if (!eng::FileSystem::fileRead(mPath, s)) return false;
        value.clear();
        for (char c : s) value.push_back(char(std::toupper((unsigned char)c)));
        mLoaded = true;
        return true;
    }
};

int main(){
    const std::string dir = "/tmp/eng_content_test";
    FileSystem::directoryCreate(dir);
    const std::string path = dir + "/greeting.txt";
    FileSystem::fileWrite(path, "hello");

    // --- TextResource loads file text ---
    TextResource tr("greeting", path);
    require(!tr.loaded(), "not loaded before load()");
    require(tr.load(), "load succeeds");
    require(tr.loaded() && tr.text()=="hello", "text loaded");
    require(tr.name()=="greeting" && tr.path()==path, "name/path retained");

    TextResource missing("nope", dir + "/nofile.txt");
    require(!missing.load(), "load fails for missing file");

    // --- ResourceCache<T>: load caches by name, dedups, reloads ---
    ResourceCache<TextResource> cache;
    TextResource* a = cache.load("greeting", path);
    require(a!=nullptr && a->text()=="hello", "cache load returns loaded resource");
    require(cache.has("greeting") && cache.size()==1, "cached one");
    TextResource* a2 = cache.load("greeting", path);
    require(a2==a, "second load returns the SAME cached instance (dedup)");
    require(cache.get("greeting")==a, "get returns cached");
    require(cache.get("absent")==nullptr, "get unknown is null");

    require(cache.load("bad", dir + "/nofile.txt")==nullptr, "failed load not cached");
    require(cache.size()==1, "failed load leaves cache size unchanged");

    // reload picks up file changes in place (same instance)
    FileSystem::fileWrite(path, "world");
    require(cache.reload("greeting") && a->text()=="world", "reload refreshes in place");
    require(!cache.reload("absent"), "reload unknown returns false");

    require(cache.remove("greeting") && !cache.has("greeting"), "remove drops entry");
    cache.load("greeting", path);
    cache.clear();
    require(cache.size()==0, "clear empties cache");

    // --- Content: type-indexed caches, distinct per resource type ---
    FileSystem::fileWrite(path, "abc");
    Content content;
    require(content.name()=="Content", "system name");
    TextResource* t = content.load<TextResource>("g", path);
    UpperResource* u = content.load<UpperResource>("g", path);   // same name, different type
    require(t!=nullptr && t->text()=="abc", "text cache load");
    require(u!=nullptr && u->value=="ABC", "upper cache load");
    require(content.get<TextResource>("g")==t, "text cache is its own map");
    require(content.get<UpperResource>("g")==u, "upper cache is a separate map");
    require(content.cache<TextResource>().size()==1, "text cache size 1");

    content.update(0.016f); // no-op, present for System interface
    content.clear();
    require(content.get<TextResource>("g")==nullptr, "clear drops all caches");

    std::cout << "ContentTests OK\n";
    return 0;
}
