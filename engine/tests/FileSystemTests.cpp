#include <eng/FileSystem.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"FileSystemTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    const std::string dir = "/tmp/eng_fs_test";
    FileSystem::directoryCreate(dir);
    const std::string f = dir + "/hello.txt";

    require(FileSystem::fileWrite(f, "abc"), "write file");
    require(FileSystem::exists(f), "exists after write");
    std::string out;
    require(FileSystem::fileRead(f, out) && out=="abc", "read back contents");
    require(FileSystem::fileWrite(f, "de", true), "append");
    FileSystem::fileRead(f, out);
    require(out=="abcde", "append concatenates");

    require(FileSystem::extension(f)==".txt", "extension");
    require(FileSystem::stem(f)=="hello", "stem");
    require(FileSystem::filename(f)=="hello.txt", "filename");

    FileSystem::fileWrite(dir + "/a.dat", "x");
    auto txts = FileSystem::directoryListFiles(dir, ".txt");
    require(txts.size()==1, "list filters by extension");

    require(FileSystem::remove(f), "delete file");
    require(!FileSystem::exists(f), "gone after delete");

    std::cout << "FileSystemTests OK\n";
    return 0;
}
