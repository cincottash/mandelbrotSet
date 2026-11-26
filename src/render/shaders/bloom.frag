// Bloom on the graphics card, the same steps as core/Bloom.cpp (which does it for saved pictures):
//   mode 0: bright pass of the picture, box-averaged into the small picture (uFactor x uFactor pixels each)
//   mode 1: Gaussian blur of the small picture along uDir
//   mode 2: the blurred small picture enlarged (bilinear) as the glow, for a "screen" blend onto the picture

out vec4 fragColor;

uniform int uMode;
uniform sampler2D uSrc;
uniform int uFactor;
uniform ivec2 uSrcSize;
uniform float uThreshold;
uniform ivec2 uDir;
uniform float uSigma;
uniform int uRadius;
uniform float uStrength;

float weight(float luma)
{
    float a = uThreshold - 0.1, b = uThreshold + 0.15;
    float t = clamp((luma - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    if (uMode == 0) {
        vec3 acc = vec3(0.0);
        int n = 0;
        for (int j = 0; j < uFactor; ++j)
            for (int i = 0; i < uFactor; ++i) {
                ivec2 q = px * uFactor + ivec2(i, j);
                if (q.x >= uSrcSize.x || q.y >= uSrcSize.y) continue;
                vec3 c = texelFetch(uSrc, q, 0).rgb;
                acc += c * weight(dot(c, vec3(0.299, 0.587, 0.114)));
                ++n;
            }
        fragColor = vec4(acc / float(max(n, 1)), 1.0);
    } else if (uMode == 1) {
        vec3 acc = vec3(0.0);
        float sum = 0.0;
        for (int i = -uRadius; i <= uRadius; ++i) {
            float k = exp(-0.5 * float(i * i) / (uSigma * uSigma));
            ivec2 q = clamp(px + uDir * i, ivec2(0), uSrcSize - 1);
            acc += texelFetch(uSrc, q, 0).rgb * k;
            sum += k;
        }
        fragColor = vec4(acc / sum, 1.0);
    } else {
        // Between the small pixels' centers, clamped at the edges, as the CPU version does.
        vec2 f = clamp((vec2(px) + 0.5) / float(uFactor) - 0.5, vec2(0.0), vec2(uSrcSize - 1));
        ivec2 p0 = ivec2(f);
        ivec2 p1 = min(p0 + 1, uSrcSize - 1);
        vec2 t = f - vec2(p0);
        vec3 a = texelFetch(uSrc, p0, 0).rgb, b = texelFetch(uSrc, ivec2(p1.x, p0.y), 0).rgb;
        vec3 c = texelFetch(uSrc, ivec2(p0.x, p1.y), 0).rgb, d = texelFetch(uSrc, p1, 0).rgb;
        vec3 v = mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
        fragColor = vec4(min(v * uStrength, vec3(1.0)), 1.0);
    }
}
