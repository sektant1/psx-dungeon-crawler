#include "EditorCamera.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
static void require(bool c,const char* m){ if(!c){ std::cerr<<"EditorCameraTests: "<<m<<'\n'; std::exit(1);} }
int main(){
    EditorCamera c;
    const glm::vec3 e0 = c.eye();
    require(std::abs(glm::length(e0 - c.target()) - 12.0f) < 1e-3f, "eye starts at distance 12");
    c.dolly(4.0f);
    require(glm::length(c.eye() - c.target()) < 12.0f, "dolly in reduces distance");
    c.dolly(-1000.0f);
    require(c.distance() > 0.4f, "distance clamped positive");
    EditorCamera d;
    d.orbit(0.5f, 0.0f);
    require(glm::length(d.eye() - e0) > 1e-3f, "orbit moves the eye");
    EditorCamera p;
    const glm::vec3 t0 = p.target();
    p.pan(glm::vec3(1.0f, 0.0f, 0.0f));
    require(glm::length(p.target() - t0 - glm::vec3(1,0,0)) < 1e-3f, "pan shifts target");
    // pitch clamp: many up-orbits must not flip past vertical
    EditorCamera q;
    for(int i=0;i<100;i++) q.orbit(0.0f, 1.0f);
    require(std::isfinite(q.eye().x) && std::isfinite(q.eye().y) && std::isfinite(q.eye().z), "eye finite after many orbits");
    std::cout << "EditorCameraTests OK\n";
    return 0;
}
