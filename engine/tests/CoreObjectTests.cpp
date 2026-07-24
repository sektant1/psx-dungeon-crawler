#include <eng/Object.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"CoreObjectTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    Object a("alpha");
    Object b("beta");
    require(a.name()=="alpha", "name stored");
    require(b.id() != a.id(), "ids unique");
    require(b.id() == a.id()+1, "ids monotonic");
    a.setName("renamed");
    require(a.name()=="renamed", "setName works");

    std::cout << "CoreObjectTests OK\n";
    return 0;
}
