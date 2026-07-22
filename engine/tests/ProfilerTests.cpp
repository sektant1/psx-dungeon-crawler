#include <eng/Profiler.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"ProfilerTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    Profiler p;
    p.beginFrame();
    p.sample("physics", 2.0);
    p.sample("render", 5.0);
    p.sample("physics", 1.0);   // same name accumulates within a frame
    p.endFrame();

    const auto& f = p.lastFrame();
    require(f.size()==2, "two distinct entries");
    double physics=-1, render=-1;
    for (const auto& e : f) {
        if (e.name=="physics") physics=e.ms;
        if (e.name=="render")  render=e.ms;
    }
    require(physics==3.0, "physics accumulated to 3ms");
    require(render==5.0, "render is 5ms");

    p.beginFrame();          // new frame resets accumulation
    p.sample("physics", 0.5);
    p.endFrame();
    require(p.lastFrame().size()==1, "new frame has one entry");
    require(p.lastFrame()[0].ms==0.5, "new frame value fresh");
    std::cout << "ProfilerTests OK\n";
    return 0;
}
