#version 330 core
// Instanced surface decal. Source 0 is a shared unit quad in the XY plane;
// uv1-4 are the per-instance stream written by eng::DecalSystem, advanced by a
// vertex attribute divisor of 1.
//
// Unlike a particle, a decal does not face the camera: it lies in the plane of
// the surface it was stamped on. The instance therefore carries that surface's
// frame, and the bitangent is recovered here with a cross product rather than
// occupying a third vec4 in the buffer.
in vec4 vertex;   // quad corner, XY in [-0.5, 0.5]
in vec2 uv0;      // quad corner UV
in vec4 uv1;      // instance: xyz world position, w world size
in vec4 uv2;      // instance: linear RGBA
in vec4 uv3;      // instance: xyz in-plane tangent
in vec4 uv4;      // instance: xyz surface normal

uniform mat4 worldViewProj;
uniform mat4 view;
uniform float farClip;

out vec2  decalUV;
out vec4  decalColour;
out vec3  decalViewNormal;
out float decalViewDepth;

void main()
{
    vec3 normal  = normalize(uv4.xyz);
    vec3 tangent = normalize(uv3.xyz);
    // Re-orthogonalise: the tangent was built on the CPU from a cardinal axis
    // and a spin, so it is close but not guaranteed perpendicular.
    tangent = normalize(tangent - normal * dot(tangent, normal));
    vec3 bitangent = cross(normal, tangent);

    vec3 world = uv1.xyz +
                 (tangent * vertex.x + bitangent * vertex.y) * uv1.w;

    decalUV = uv0;
    decalColour = uv2;
    decalViewNormal = mat3(view) * normal;

    // The batch never moves its scene node, so instance positions are already
    // world-space and worldViewProj degenerates to viewProj.
    gl_Position = worldViewProj * vec4(world, 1.0);
    decalViewDepth = length((view * vec4(world, 1.0)).xyz) / max(farClip, 0.0001);
}
