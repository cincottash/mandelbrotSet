// Colors the Buddhabrot counts: as the Nebulabrot (red, green and blue from the three iteration limits) or through
// the palette (the three counts together). Row 0 of the output is the top of the picture, as everywhere here.

out vec4 fragColor;

layout(std430, binding = 7) readonly buffer Hist { uint hist[]; };
layout(std430, binding = 8) readonly buffer HistMax { uint histMax[]; };
uniform sampler2D uPalette;
uniform ivec2 uSize;
uniform bool uNebula;
uniform float uExposure;
uniform float uGamma;
uniform float uDensity;
uniform float uOffset;
uniform vec3 uBg;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    uint base = uint(p.y * uSize.x + p.x) * 3u;
    vec3 n = vec3(float(hist[base]), float(hist[base + 1u]), float(hist[base + 2u]));
    // Each channel against its own brightest pixel, on a square-root scale before the gamma, so faint orbits show.
    vec3 m = max(vec3(float(histMax[0]), float(histMax[1]), float(histMax[2])), vec3(1.0));
    vec3 v = clamp(pow(sqrt(n / m) * uExposure, vec3(uGamma * 2.0)), 0.0, 1.0);
    vec3 col;
    if (uNebula) {
        col = uBg + (1.0 - uBg) * v;
    } else {
        float b = clamp(dot(v, vec3(0.5, 0.3, 0.2)), 0.0, 1.0);
        col = mix(uBg, texture(uPalette, vec2(fract(b * uDensity + uOffset), 0.5)).rgb, clamp(b * 4.0, 0.0, 1.0));
    }
    fragColor = vec4(col, 1.0);
}
