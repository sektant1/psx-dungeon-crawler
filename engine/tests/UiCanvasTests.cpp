#include <eng/ui/UiCanvas.h>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "UiCanvasTests: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    eng::ui::UiCanvas canvas;

    canvas.begin({1281.0f, 961.0f}, {640, 480});
    require(canvas.scale() == 2, "largest fitting integer scale is selected");
    require(canvas.size() == glm::ivec2(641, 481),
            "odd output dimensions ceil instead of leaving stale edge strips");
    require(canvas.size().x * canvas.scale() >= 1281 &&
                canvas.size().y * canvas.scale() >= 961,
            "virtual surface covers full output");

    canvas.begin({960.0f, 720.0f}, {640, 480}, {2.0f, 2.0f});
    require(canvas.scale() == 3 && canvas.size() == glm::ivec2(640, 480),
            "high-DPI fit resolves in physical pixels, not logical points");

    canvas.begin({1280.0f, 720.0f}, {640, 480}, {1.25f, 1.25f});
    require(canvas.size() == glm::ivec2(1600, 900),
            "fractional framebuffer scale preserves physical pixel coverage");

    canvas.beginTarget({10.0f, 20.0f}, {0, -2}, 99, nullptr);
    require(canvas.size() == glm::ivec2(1, 1) && canvas.scale() == 8,
            "embedded targets clamp degenerate size and scale");

    std::cout << "UiCanvasTests: OK\n";
    return 0;
}
