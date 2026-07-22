#include <eng/Events.h>
#include <cstdlib>
#include <iostream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"EventsTests: "<<m<<'\n'; std::exit(1);} }

struct DamageEvent : Event { DamageEvent(int a):Event("Damage"),amount(a){} int amount; };
struct HealEvent   : Event { HealEvent(int a):Event("Heal"),amount(a){}   int amount; };

struct Player {
    int hp = 100;
    void onDamage(DamageEvent& e){ hp -= e.amount; }
    void onHeal(HealEvent& e){ hp += e.amount; }
};

int main(){
    EventBus bus;
    Player p;
    bus.connect<DamageEvent>(&p, &Player::onDamage);
    bus.connect<HealEvent>(&p, &Player::onHeal);

    DamageEvent d(30); bus.dispatch(d);
    require(p.hp==70, "damage handler ran");
    HealEvent h(10); bus.dispatch(h);
    require(p.hp==80, "heal handler ran; type routing correct");

    // unrelated event type with no subscribers is a no-op
    struct NoiseEvent : Event { NoiseEvent():Event("Noise"){} } n;
    bus.dispatch(n);
    require(p.hp==80, "unsubscribed type ignored");

    bus.disconnect(&p);
    DamageEvent d2(50); bus.dispatch(d2);
    require(p.hp==80, "no handlers after disconnect");

    std::cout << "EventsTests OK\n";
    return 0;
}
