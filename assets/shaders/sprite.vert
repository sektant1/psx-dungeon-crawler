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
flat out vec2 spriteCell;
flat out vec2 spriteInvGrid;
smooth out vec4 spriteColour;

void main()
{
    vec2 grid = max(floor(spriteGrid + 0.5), vec2(1.0));
    float count = clamp(floor(spriteFrameCount + 0.5), 1.0,
                        grid.x * grid.y);
    float t = max(0.0, time + spritePhase);
    float frame = spriteFps > 0.0 ? mod(floor(t * spriteFps), count) : 0.0;
    vec2 cell = vec2(mod(frame, grid.x), floor(frame / grid.x));
    // Preserve 0..1 vertex endpoints. Wrapping here would turn both 0 and 1
    // into zero and collapse an entire billboard to a single texel.
    spriteUV = uv0 * spriteUvScale + spriteScroll * t;
    spriteCell = cell;
    spriteInvGrid = 1.0 / grid;
    spriteColour = colour;
    gl_Position = worldViewProj * vertex;
}
