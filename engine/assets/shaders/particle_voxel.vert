#version 330 core
// Instanced unit cube. Same per-instance stream as the sprite batch; the
// flipbook slot (uv3.y) is unused here.
in vec4 vertex;   // cube corner, half-extent 0.5
in vec3 normal;   // face normal, one per split vertex
in vec4 uv1;      // instance: xyz world position, w world size
in vec4 uv2;      // instance: linear RGBA
in vec2 uv3;      // instance: x yaw in radians

uniform mat4 worldViewProj;
uniform mat4 view;

out vec3  voxelNormal;     // world space, drives the flat face tone
out vec3  voxelNormalVS;   // view space, for the compositor's edge metadata
out vec4  voxelColour;
out float voxelViewDepth;

void main()
{
    float s = sin(uv3.x);
    float c = cos(uv3.x);
    // Yaw only. A full per-particle orientation would need a second instance
    // attribute, and chunky gore reads fine spinning about the world up axis.
    vec3 local = vec3(vertex.x * c - vertex.z * s,
                      vertex.y,
                      vertex.x * s + vertex.z * c);
    vec3 rotatedNormal = vec3(normal.x * c - normal.z * s,
                              normal.y,
                              normal.x * s + normal.z * c);

    vec3 world = uv1.xyz + local * uv1.w;
    voxelNormal = rotatedNormal;
    voxelNormalVS = mat3(view) * rotatedNormal;
    voxelColour = uv2;
    gl_Position = worldViewProj * vec4(world, 1.0);
    voxelViewDepth = length((view * vec4(world, 1.0)).xyz);
}
