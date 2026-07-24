#include "InspectorRegistry.h"

#include "ComponentRegistry.h"

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "InspectorRegistryTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const InspectorRegistry& insp = inspectorRegistry();
    for (const mapio::ComponentType& t : mapio::coreRegistry().types())
        require(insp.find(t.stableTypeId) != nullptr,
                "every serializable component has an inspector");
    std::cout << "InspectorRegistryTests OK\n";
    return 0;
}
