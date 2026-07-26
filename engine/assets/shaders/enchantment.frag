#version 330 core
in vec3 objectPosition;
in vec3 objectNormal;
uniform vec4 enchantColour;
uniform float enchantStrength;
uniform float enchantRuneScale;
uniform vec3 enchantScroll;
uniform float enchantPulseSpeed;
uniform float enchantPulseDepth;
uniform float enchantEdgeIntensity;
uniform float time;
uniform vec3 cameraPositionObject;
layout(location = 0) out vec4 fragColour;

float runeField(vec2 coordinate)
{
    vec2 cell = floor(coordinate);
    vec2 p = fract(coordinate) - 0.5;
    float hash = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);

    float diamond = abs(abs(p.x) + abs(p.y) - 0.27);
    float crossRune = min(max(abs(p.x) - 0.045, abs(p.y) - 0.30),
                          max(abs(p.y) - 0.045, abs(p.x) - 0.30));
    float ring = abs(length(p) - 0.25);
    float dist = hash < 0.34 ? diamond : (hash < 0.67 ? crossRune : ring);

    float rune = 1.0 - smoothstep(0.035, 0.085, dist);
    float core = 1.0 - smoothstep(0.0, 0.025, dist);
    return rune * (0.82 + core * 0.38);
}

void main()
{
    vec3 normal = normalize(objectNormal);
    vec3 triplanarWeights = pow(abs(normal), vec3(4.0));
    triplanarWeights /= max(dot(triplanarWeights, vec3(1.0)), 0.0001);

    // Scroll before scaling so enchantScroll remains in object-space units
    // per second regardless of the requested rune density.
    vec3 runePosition =
        (objectPosition + enchantScroll * time) * enchantRuneScale;
    float runes =
        runeField(runePosition.yz) * triplanarWeights.x +
        runeField(runePosition.xz) * triplanarWeights.y +
        runeField(runePosition.xy) * triplanarWeights.z;

    float pulseWave = 0.5 + 0.5 * sin(time * enchantPulseSpeed);
    float pulse = 1.0 - enchantPulseDepth +
                  enchantPulseDepth * pulseWave;
    vec3 viewDirection =
        normalize(cameraPositionObject - objectPosition);
    float fresnel = pow(
        1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    float intensity = enchantColour.a * enchantStrength *
                      (runes * pulse + fresnel * enchantEdgeIntensity);
    vec3 colour = enchantColour.rgb * intensity +
                  vec3(runes * intensity * 0.22);
    fragColour = vec4(colour, intensity);
}
