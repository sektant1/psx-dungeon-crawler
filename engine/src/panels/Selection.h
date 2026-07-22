#pragma once
#include <eng/Handles.h>
namespace eng::ui {
struct Selection { NodeHandle node{}; bool has() const { return node.valid(); } };
}
