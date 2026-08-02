#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColour;

layout(set = 0, binding = 0, std140) uniform Transform {
    vec4 offsetScale;
} transformData;

layout(push_constant) uniform DrawConstants {
    vec4 tint;
} drawConstants;

layout(location = 0) out vec3 colour;
layout(location = 1) out vec2 uv;

void main()
{
    vec2 position = inPosition * transformData.offsetScale.zw +
                    transformData.offsetScale.xy;
    gl_Position = vec4(position, 0.0, 1.0);
    colour = inColour * drawConstants.tint.rgb;
    uv = inPosition * 0.5 + 0.5;
}
