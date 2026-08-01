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
uniform float enchantBandCount;
uniform float enchantPixelScale;
uniform float enchantCoreBoost;
uniform float time;
uniform vec3 cameraPositionObject;
layout(location = 0) out vec4 fragColour;

vec2 runeField(vec2 coordinate)
{
    vec2 cell = floor(coordinate);
    vec2 p = fract(coordinate) - 0.5;
    float hash = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);

    float diamond = abs(abs(p.x) + abs(p.y) - 0.29);
    float crossRune = min(max(abs(p.x) - 0.075, abs(p.y) - 0.32),
                          max(abs(p.y) - 0.075, abs(p.x) - 0.32));
    float ring = abs(max(abs(p.x), abs(p.y)) - 0.27);
    float dist = hash < 0.34 ? diamond : (hash < 0.67 ? crossRune : ring);
    float runeBody = 1.0 - step(0.105, dist);
    float runeCore = 1.0 - step(0.047, dist);
    return vec2(runeBody, runeCore);
}

float quantizeBand(float value)
{
    float bands = max(enchantBandCount, 2.0);
    return floor(clamp(value, 0.0, 1.0) * bands + 0.5) / bands;
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
    runePosition =
        floor(runePosition * enchantPixelScale + 0.5) /
        max(enchantPixelScale, 1.0);
    vec2 runes =
        runeField(runePosition.yz) * triplanarWeights.x +
        runeField(runePosition.xz) * triplanarWeights.y +
        runeField(runePosition.xy) * triplanarWeights.z;
    float runeBody = quantizeBand(runes.x);
    float runeCore = quantizeBand(runes.y);

    float pulseWave = 0.5 + 0.5 * sin(time * enchantPulseSpeed);
    float pulse = quantizeBand(1.0 - enchantPulseDepth +
                               enchantPulseDepth * pulseWave);
    vec3 viewDirection =
        normalize(cameraPositionObject - objectPosition);
    float fresnel = quantizeBand(pow(
        1.0 - max(dot(normal, viewDirection), 0.0), 3.0));
    float bodyIntensity = enchantColour.a * enchantStrength *
                          (runeBody * pulse +
                           fresnel * enchantEdgeIntensity);
    float coreIntensity = enchantColour.a * enchantStrength *
                          runeCore * enchantCoreBoost;
    float intensity = bodyIntensity + coreIntensity;
    vec3 colour = enchantColour.rgb * bodyIntensity +
                  mix(enchantColour.rgb, vec3(1.0), 0.38) *
                  coreIntensity;
    fragColour = vec4(colour, intensity);
}
