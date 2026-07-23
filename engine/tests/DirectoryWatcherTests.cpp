#include <eng/DirectoryWatcher.h>
#include <eng/FileSystem.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"DirectoryWatcherTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    const std::string dir = "/tmp/eng_watch_test";
    FileSystem::directoryCreate(dir);
    // clean slate
    for (const auto& f : FileSystem::directoryListFiles(dir)) FileSystem::remove(f);

    DirectoryWatcher w(dir);
    w.poll();  // establish baseline (empty)

    FileSystem::fileWrite(dir + "/level.toml", "a");
    auto added = w.poll();
    require(added.size()==1, "one change detected");
    require(added[0].kind==FileChange::Added, "kind is Added");
    require(FileSystem::filename(added[0].path)=="level.toml", "path reported");

    require(w.poll().empty(), "no change when nothing touched");

    // modify (bump mtime beyond filesystem resolution)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    FileSystem::fileWrite(dir + "/level.toml", "ab");
    auto mod = w.poll();
    require(mod.size()==1 && mod[0].kind==FileChange::Modified, "modification detected");

    FileSystem::remove(dir + "/level.toml");
    auto del = w.poll();
    require(del.size()==1 && del[0].kind==FileChange::Removed, "removal detected");

    std::cout << "DirectoryWatcherTests OK\n";
    return 0;
}
