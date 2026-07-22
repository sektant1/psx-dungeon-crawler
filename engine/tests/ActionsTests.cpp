#include <eng/Action.h>
#include <eng/Actions.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"ActionsTests: "<<m<<'\n'; std::exit(1);} }
static bool near(float a, float b){ return std::fabs(a-b) < 1e-4f; }

int main(){
    // easing endpoints
    require(near(easeApply(0.0f, Ease::QuadIn), 0.0f), "ease t0");
    require(near(easeApply(1.0f, Ease::QuadIn), 1.0f), "ease t1");
    require(near(easeApply(0.5f, Ease::QuadIn), 0.25f), "quadin midpoint");

    // ActionDelay: consumes dt, reports leftover when it finishes
    ActionDelay d(1.0f);
    require(d.update(0.4f)==0.0f && !d.finished(), "delay partway");
    float leftover = d.update(0.9f);
    require(d.finished() && near(leftover, 0.3f), "delay finishes with leftover");

    // ActionCall: fires once, finishes, passes all dt through
    int calls = 0;
    ActionCall c([&]{ ++calls; });
    float lo = c.update(0.5f);
    require(calls==1 && c.finished() && near(lo, 0.5f), "call fires once");

    // ActionProperty<float>: linear tween, captures start on first update
    float x = 0.0f;
    ActionProperty<float> p(x, 10.0f, 1.0f, Ease::Linear);
    p.update(0.5f);
    require(near(x, 5.0f), "float tween halfway");
    p.update(0.5f);
    require(near(x, 10.0f) && p.finished(), "float tween done");

    // ActionProperty<glm::vec3>
    glm::vec3 v(0.0f);
    ActionProperty<glm::vec3> pv(v, glm::vec3(2.0f,4.0f,6.0f), 2.0f, Ease::Linear);
    pv.update(1.0f);
    require(near(v.x,1.0f)&&near(v.y,2.0f)&&near(v.z,3.0f), "vec3 tween halfway");

    // --- Sequence: serial; instantaneous Call chains within a frame ---
    Actions actions;
    int order = 0, first = -1, second = -1;
    float y = 0.0f;
    ActionSequence& seq = actions.sequence();
    seq.call([&]{ first = order++; })
       .property(y, 10.0f, 1.0f, Ease::Linear)
       .call([&]{ second = order++; });
    actions.update(0.5f);
    require(first==0, "first call ran immediately");
    require(near(y,5.0f), "seq tween halfway");
    require(second==-1, "second call waits for tween");
    require(actions.activeCount()==1, "sequence still active");
    actions.update(0.5f);
    require(near(y,10.0f) && second==1, "tween done then second call");
    require(actions.activeCount()==0, "sequence pruned when empty");

    // --- Group: parallel ---
    float a1 = 0.0f, a2 = 0.0f;
    ActionGroup& grp = actions.group();
    grp.property(a1, 4.0f, 1.0f, Ease::Linear)
       .property(a2, 8.0f, 1.0f, Ease::Linear);
    actions.update(0.5f);
    require(near(a1,2.0f) && near(a2,4.0f), "group runs in parallel");
    actions.update(0.5f);
    require(near(a1,4.0f) && near(a2,8.0f) && actions.activeCount()==0, "group done + pruned");

    std::cout << "ActionsTests OK\n";
    return 0;
}
