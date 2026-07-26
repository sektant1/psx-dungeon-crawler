#version 330 core
in vec4 vertex;
in vec3 normal;
uniform mat4 worldViewProj;
out vec3 objectPosition;
out vec3 objectNormal;
void main()
{
    objectPosition = vertex.xyz;
    objectNormal = normal;
    gl_Position = worldViewProj * vertex;
}
