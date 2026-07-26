#include "../src/SceneRegistry.h"
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"SceneRegistryTests: "<<m<<'\n'; std::exit(1);} }
static NodeHandle H(uint32_t id){ return NodeHandle{id}; }
int main(){
    SceneRegistry r;
    r.addNode(H(1), NodeHandle{}, "root");
    r.addNode(H(2), H(1), "child");
    r.addNode(H(3), H(1), "child2");
    require(r.roots().size()==1, "one root");
    require(r.find(H(1))->children.size()==2, "root has two children");
    require(r.find(H(2))->name=="child", "name stored");
    r.addAttachment(H(2), {NodeAttachKind::Light, 7, "torch"});
    r.addAttachment(H(3), {NodeAttachKind::Particles, 8, "smoke"});
    require(r.find(H(2))->attachments.size()==1, "attachment recorded");
    r.removeAttachment(NodeAttachKind::Particles, 8);
    require(r.find(H(3))->attachments.empty(), "attachment removed by handle");
    require(r.find(H(2))->attachments.size()==1, "unrelated attachment retained");
    require(r.autoName(H(5))=="Node 5", "autoName");
    r.removeNode(H(1)); // recursive
    require(r.find(H(1))==nullptr && r.find(H(2))==nullptr && r.find(H(3))==nullptr, "recursive prune");
    require(r.roots().empty(), "roots cleared after prune");
    r.addNode(H(9), NodeHandle{}, "a");
    r.clear();
    require(r.find(H(9))==nullptr && r.roots().empty(), "clear empties");
    std::cout << "SceneRegistryTests OK\n";
    return 0;
}
