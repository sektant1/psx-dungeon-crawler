#include <editor/scene/PaintSlot.h>

#include <cmath>

namespace ed {

std::string gridPaintSlot(const game::content::CellPlacement& cell)
{
    // The level is part of it: two floors stacked on different work planes are
    // two pieces, not one painted twice.
    return std::to_string(cell.col) + ',' + std::to_string(cell.row) + ',' +
           std::to_string(int(cell.edge)) + ',' +
           std::to_string(int(std::lround(cell.level * 100.0f)));
}

std::string freePaintSlot(const glm::vec3& position, float spacing)
{
    const float step = spacing > 0.0f ? spacing : kFreePaintSpacing;
    const auto quantize = [step](float value) {
        return std::to_string(long(std::lround(value / step)));
    };
    return "free," + quantize(position.x) + ',' + quantize(position.y) + ',' +
           quantize(position.z);
}

} // namespace ed
