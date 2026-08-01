#version 330 core
// Instanced camera-facing particle quad. Source 0 is a shared unit quad; uv1-3
// are the per-instance stream written by eng::ParticleBatch, advanced by a
// vertex attribute divisor of 1.
in vec4 vertex;   // quad corner, XY in [-0.5, 0.5]
in vec2 uv0;      // quad corner UV, before flipbook remap
in vec4 uv1;      // instance: xyz world position, w world size
in vec4 uv2;      // instance: linear RGBA
in vec2 uv3;      // instance: x rotation in radians, y flipbook frame

uniform mat4 worldViewProj;
uniform mat4 view;
// Where this material's animation lives inside its texture, in UV. Cell is the
// size of one frame, origin is frame 0's corner, and perRow is how many frames
// run along a row before the strip wraps down one cell. A whole-sheet grid is
// the special case origin = 0, cell = 1/(cols,rows), perRow = cols -- which is
// why one set of uniforms covers both a dedicated flipbook PNG and a strip
// carved out of a shared effect sheet.
uniform vec2 flipbookCell;
uniform vec2 flipbookOrigin;
uniform float flipbookPerRow;

out vec2  particleUV;
out vec4  particleColour;
out float particleViewDepth;

void main()
{
    // The view matrix is the inverse of the camera's world transform, so its
    // columns read out as the camera's world-space axes. Taking right/up from
    // there billboards the quad without a per-instance matrix.
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);

    float s = sin(uv3.x);
    float c = cos(uv3.x);
    vec2 corner = vec2(vertex.x * c - vertex.y * s,
                       vertex.x * s + vertex.y * c);
    vec3 world = uv1.xyz + (right * corner.x + up * corner.y) * uv1.w;

    float perRow = max(flipbookPerRow, 1.0);
    float frame = floor(max(uv3.y, 0.0));
    float col = mod(frame, perRow);
    float row = floor(frame / perRow);
    particleUV = flipbookOrigin + (uv0 + vec2(col, row)) * flipbookCell;
    particleColour = uv2;

    // The batch never moves its scene node, so instance positions are already
    // world-space and worldViewProj degenerates to viewProj.
    gl_Position = worldViewProj * vec4(world, 1.0);
    particleViewDepth = length((view * vec4(world, 1.0)).xyz);
}
