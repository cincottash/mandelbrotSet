// Turns the raw numbers of one render into colors: one output pixel averages uSS x uSS raw samples.
// Row 0 of every texture here is the top of the picture.

out vec4 fragColor;

uniform sampler2D uRaw0;      // mu (-1 inside), distance (raw px), normal.xy
uniform sampler2D uRaw1;      // |z| when it first passed 2, angle, stripes, inside value
uniform sampler2D uPalette;   // 1024 x 1, repeating
uniform int uSS;
uniform ivec2 uOutSize;       // the whole picture's size in pixels
uniform ivec2 uOrigin;        // where this part starts in the whole picture

uniform int uStyle;           // 0 gradient, 1 sine waves, 2 moire, 3 distance, 4 stripes, 5 angle
uniform int uScale;           // 0 linear, 1 square root, 2 log
uniform float uDensity;
uniform float uOffset;
uniform vec3 uSineFreq;
uniform vec3 uSinePhase;
uniform float uSineBase;
uniform float uSineHeight;
uniform float uMoireScale;
uniform vec3 uMoireTint;
uniform float uLightStrength;
uniform vec3 uLightDir;
uniform float uRelief;
uniform float uSpecular;
uniform float uEdgeGlow;
uniform float uEdgeWidth;
uniform vec3 uEdgeColor;
uniform int uInterior;        // 0 solid, 1 diagonal gradient, 2 depth
uniform vec3 uIn1;
uniform vec3 uIn2;
uniform float uHue;           // radians
uniform float uSat;
uniform float uBright;
uniform float uContrast;
uniform float uGamma;
uniform float uVignette;
uniform float uGrain;
uniform float uSeed;

const float TAU = 6.28318530717958647692;

vec3 palette(float t) { return texture(uPalette, vec2(fract(t), 0.5)).rgb; }

float bandValue(float mu)
{
    float s;
    if (uScale == 1) s = sqrt(max(mu, 0.0)) * 0.6;
    else if (uScale == 2) s = log2(max(mu, 0.0) + 1.0) * 0.5;
    else s = mu / 24.0;
    return s * uDensity + uOffset;
}

// The original Python version's exterior: brightness from where |z| first passed 2, times |cos(x * y)| of the
// pixel's own row and column. At this size cos(x * y) changes faster than the pixels, which draws the folds.
vec3 moire(float firstR, ivec2 px)
{
    float v = clamp(1.5 - firstR / 4.0, 0.0, 1.0);
    double arg = double(uMoireScale) * double(px.x) * double(px.y);
    const double TAU_D = 6.283185307179586476925LF;
    float r = float(arg - floor(arg / TAU_D) * TAU_D);
    return v * abs(cos(r)) * uMoireTint;
}

vec3 shade(vec3 col, vec2 nrm, bool inside)
{
    if (inside || uLightStrength <= 0.0) return col;
    vec3 N = normalize(vec3(nrm, 1.0 / max(uRelief, 0.01)));
    float diffuse = max(dot(N, uLightDir), 0.0) / max(uLightDir.z, 0.05);
    vec3 R = reflect(-uLightDir, N);
    float spec = pow(max(R.z, 0.0), 24.0) * uSpecular;
    vec3 lit = col * clamp(diffuse, 0.0, 2.2) + vec3(spec);
    return mix(col, lit, uLightStrength);
}

vec3 sampleColor(ivec2 rawPx, ivec2 outPx)
{
    vec4 r0 = texelFetch(uRaw0, rawPx, 0);
    vec4 r1 = texelFetch(uRaw1, rawPx, 0);
    bool inside = r0.x < 0.0;
    vec3 col;
    if (inside) {
        if (uInterior == 1) {
            float t = (float(outPx.x) * float(outPx.y)) / (float(uOutSize.x) * float(uOutSize.y));
            col = mix(uIn1, uIn2, clamp(t, 0.0, 1.0));
        } else if (uInterior == 2) {
            float t = clamp(sqrt(r1.w) / 2.0, 0.0, 1.0);
            col = mix(uIn1, uIn2, pow(t, 0.6));
        } else {
            col = uIn1;
        }
        return col;
    }
    float mu = r0.x;
    float t = bandValue(mu);
    if (uStyle == 1) {
        col = clamp(vec3(uSineBase) + uSineHeight * cos(TAU * (uSineFreq * t + uSinePhase)), 0.0, 1.0);
    } else if (uStyle == 2) {
        col = moire(r1.x, outPx);
    } else if (uStyle == 3) {
        float de = max(r0.y / float(uSS), 1.0e-6);
        col = palette(log2(de) * 0.25 * uDensity + uOffset);
    } else if (uStyle == 4) {
        col = palette(r1.z * uDensity + uOffset);
    } else if (uStyle == 5) {
        col = palette(r1.y / TAU * uDensity + uOffset);
    } else {
        col = palette(t);
    }
    col = shade(col, r0.zw, false);
    if (uEdgeGlow > 0.0) {
        float de = r0.y / float(uSS);
        float g = uEdgeGlow * exp(-de / max(uEdgeWidth, 0.05));
        col = mix(col, uEdgeColor, clamp(g, 0.0, 1.0));
    }
    return col;
}

float hash(vec2 p)
{
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

void main()
{
    ivec2 localPx = ivec2(gl_FragCoord.xy);
    ivec2 outPx = localPx + uOrigin;   // the pixel within the whole picture (exports are drawn in pieces)
    vec3 acc = vec3(0.0);
    for (int j = 0; j < uSS; ++j)
        for (int i = 0; i < uSS; ++i) acc += sampleColor(localPx * uSS + ivec2(i, j), outPx);
    vec3 col = acc / float(uSS * uSS);

    // Effects, in order: hue, saturation, contrast and brightness, gamma, vignette, grain.
    if (uHue != 0.0) {
        const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
        const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
        vec3 yiq = toYiq * col;
        float c = cos(uHue), s = sin(uHue);
        yiq.yz = vec2(yiq.y * c - yiq.z * s, yiq.y * s + yiq.z * c);
        col = toRgb * yiq;
    }
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(luma), col, uSat);
    col = (col - 0.5) * uContrast + 0.5 + uBright;
    col = pow(clamp(col, 0.0, 1.0), vec3(uGamma));
    if (uVignette > 0.0) {
        vec2 c = (vec2(outPx) + 0.5) / vec2(uOutSize) - 0.5;
        float r = length(c * vec2(float(uOutSize.x) / float(uOutSize.y), 1.0)) / length(vec2(float(uOutSize.x) / float(uOutSize.y), 1.0) * 0.5);
        col *= 1.0 - uVignette * smoothstep(0.35, 1.05, r);
    }
    if (uGrain > 0.0) col += (hash(vec2(outPx) + uSeed) - 0.5) * 0.35 * uGrain;
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
