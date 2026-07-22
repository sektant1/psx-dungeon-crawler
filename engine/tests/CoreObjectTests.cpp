#include <eng/Object.h>
#include <eng/GameObject.h>
#include <eng/GameSession.h>
#include <cstdlib>
#include <iostream>
#include <memory>
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

    // --- Component / Entity ---
    struct Health : Component { Health():Component("Health"){} int hp=10; };
    struct Speed  : Component { Speed():Component("Speed"){}  float v=1.0f; };

    GameObject go("hero");
    go.setArchetype("Hero");
    require(go.archetype()=="Hero", "archetype stored");
    Health* h = static_cast<Health*>(go.addComponent(std::make_unique<Health>()));
    go.addComponent(std::make_unique<Speed>());
    require(h->owner()==&go, "owner set on add");
    require(go.getComponent<Health>()==h, "getComponent finds by type");
    require(go.getComponent<Speed>()!=nullptr, "getComponent finds second type");
    require(go.components().size()==2, "two components held");
    go.removeAllComponents();
    require(go.components().empty(), "removeAllComponents clears");
    require(go.getComponent<Health>()==nullptr, "getComponent null after clear");

    // --- Space / GameSession ---
    GameSession session("session");
    Space* world = session.createSpace("World");
    require(world!=nullptr, "space created");
    require(session.getSpace("World")==world, "getSpace by name");
    GameObject* g1 = world->createObject("goblin");
    world->createObject("goblin2");
    require(world->objects().size()==2, "two objects in space");
    require(g1->name()=="goblin", "object name");
    world->destroyObject(g1);       // deferred: still present until flush
    require(world->objects().size()==2, "destroy is deferred");
    world->flushDestroyed();
    require(world->objects().size()==1, "flush removes marked object");
    world->clear();
    require(world->objects().empty(), "clear empties space");

    std::cout << "CoreObjectTests OK\n";
    return 0;
}
