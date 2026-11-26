// Sphere: the flat picture wrapped onto a lit ball over a background (the same math as core/Sphere.cpp, which does
// it for saved pictures). Row 0 of the textures is the top of the picture, as gl_FragCoord counts here.

out vec4 fragColor;

uniform sampler2D uSrc;       // the flat picture, linear filtering, clamped at the edges
uniform ivec2 uSize;
uniform float uRadius;        // pixels
uniform vec3 uLight;          // toward the light, y up, normalized
uniform float uShade;
uniform float uShine;
uniform vec3 uBg;

const float HALF_PI = 1.57079632679489661923;

void main()
{
    vec2 p = gl_FragCoord.xy;   // pixel centers at +0.5
    vec2 c = vec2(uSize) * 0.5;
    vec2 d = (p - c) / uRadius;
    vec2 q = vec2(d.x, -d.y);
    float r2 = dot(q, q);
    vec2 s = vec2((p.x - c.x) / (uRadius * 0.9), (p.y - (c.y + uRadius * 1.02)) / (uRadius * 0.18));
    float shadow = 0.55 * exp(-dot(s, s));
    vec3 col = uBg * (1.0 - shadow);
    float edge = clamp((1.0 - sqrt(r2)) * uRadius + 0.5, 0.0, 1.0);
    if (edge > 0.0) {
        float z = sqrt(1.0 - min(r2, 1.0));
        float lon = atan(q.x, z), lat = asin(clamp(q.y, -1.0, 1.0));
        vec2 at = c + vec2(lon, -lat) / HALF_PI * (float(uSize.y) * 0.5);
        vec3 base = texture(uSrc, at / vec2(uSize)).rgb;
        vec3 N = vec3(q, z);
        float diffuse = max(dot(N, uLight), 0.0);
        float k = 1.0 + uShade * (0.25 + 0.95 * diffuse - 1.0);
        vec3 H = normalize(uLight + vec3(0.0, 0.0, 1.0));
        float spec = pow(max(dot(N, H), 0.0), 48.0) * uShine;
        col = mix(col, min(base * k + vec3(spec), vec3(1.0)), edge);
    }
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
