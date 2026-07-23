#include <eng/TextResource.h>
#include <eng/FileSystem.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"ContentTests: "<<m<<'\n'; std::exit(1);} }

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

    std::cout << "ContentTests OK\n";
    return 0;
}
