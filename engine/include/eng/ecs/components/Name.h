#pragma once
#include <string>

namespace eng::ecs {

// Human-readable label: outliner rows, log lines, the name given to the
// renderer node. Optional -- an entity with no Name is anonymous, which most
// of a cooked level is.
struct Name {
    std::string value;
};

} // namespace eng::ecs
