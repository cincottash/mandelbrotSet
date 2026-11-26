// Draws a finished picture on the screen, moved, zoomed and turned to the current view, so the picture follows
// every movement at once while the next render is computed. Row 0 of the picture is its top.

out vec4 fragColor;

uniform sampler2D uImage;
uniform vec2 uImageSize;     // pixels of the picture
uniform vec2 uScreen;        // pixels of the screen area
uniform mat2 uM;             // current view units -> picture view units
uniform vec2 uT;
uniform vec3 uBackground;

void main()
{
    vec2 px = vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);   // from the top-left
    float half_h = 0.5 * uScreen.y;
    vec2 q = vec2((px.x - 0.5 * uScreen.x) / half_h, -(px.y - half_h) / half_h);
    vec2 qo = uM * q + uT;
    float ih = 0.5 * uImageSize.y;
    vec2 ip = vec2(qo.x * ih + 0.5 * uImageSize.x, -qo.y * ih + ih);
    vec2 uv = ip / uImageSize;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        fragColor = vec4(uBackground, 1.0);
        return;
    }
    fragColor = vec4(texture(uImage, uv).rgb, 1.0);
}
