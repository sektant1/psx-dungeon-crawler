#include <eng/Factory.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"FactoryTests: "<<m<<'\n'; std::exit(1);} }

struct Health : Component { Health():Component("Health"){} };
struct Speed  : Component { Speed():Component("Speed"){}  };

int main(){
    Factory f;
    ENG_REGISTER_COMPONENT(f, Health);
    ENG_REGISTER_COMPONENT(f, Speed);
    require(f.has("Health"), "registered Health");
    require(f.has("Speed"), "registered Speed");
    require(!f.has("Missing"), "unknown reports false");
    require(f.registeredNames().size()==2, "two registered names");

    auto c = f.create("Health");
    require(c!=nullptr, "create returns component");
    require(c->name()=="Health", "created has right name");
    require(dynamic_cast<Health*>(c.get())!=nullptr, "created is Health type");

    auto miss = f.create("Missing");
    require(miss==nullptr, "unknown create returns null");
    std::cout << "FactoryTests OK\n";
    return 0;
}
