#version 330 core

in vec4 vertex;
in vec2 uv0;
in vec4 colour;

uniform mat4 worldViewProj;
uniform float time;
uniform vec2 spriteGrid;
uniform float spriteFrameCount;
uniform float spriteFps;
uniform vec2 spriteScroll;
uniform vec2 spriteUvScale;
uniform float spritePhase;

smooth out vec2 spriteUV;
smooth out vec4 spriteColour;

void main()
{
    vec2 grid = max(floor(spriteGrid + 0.5), vec2(1.0));
    float count = clamp(floor(spriteFrameCount + 0.5), 1.0,
                        grid.x * grid.y);
    float t = max(0.0, time + spritePhase);
    float frame = spriteFps > 0.0 ? mod(floor(t * spriteFps), count) : 0.0;
    vec2 cell = vec2(mod(frame, grid.x), floor(frame / grid.x));
    // Wrap inside the selected atlas cell. Without this, scrolling an atlas
    // leaks into neighbouring frames instead of animating the current frame.
    vec2 localUV = fract(uv0 * spriteUvScale + spriteScroll * t);
    spriteUV = (localUV + cell) / grid;
    spriteColour = colour;
    gl_Position = worldViewProj * vertex;
}
