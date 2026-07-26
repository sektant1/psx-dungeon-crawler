#version 330 core
in vec4 vertex;
in vec4 colour;
in vec2 uv0;
uniform mat4 worldViewProj;
uniform float time;
uniform vec2 atlasGrid;
uniform float atlasRow;
uniform float atlasFrames;
uniform float atlasFps;
out vec2 particleUV;
out vec4 particleColour;
void main()
{
    vec2 grid = max(atlasGrid, vec2(1.0));
    float count = clamp(atlasFrames, 1.0, grid.x);
    float frame = atlasFps > 0.0 ? mod(floor(time * atlasFps), count) : 0.0;
    particleUV = (uv0 + vec2(frame, clamp(atlasRow, 0.0, grid.y - 1.0))) / grid;
    particleColour = colour;
    gl_Position = worldViewProj * vertex;
}
