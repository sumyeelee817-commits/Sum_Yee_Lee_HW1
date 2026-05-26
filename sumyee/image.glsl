#version 400
uniform vec3 iResolution;
uniform sampler2D texMain; 

layout(location = 0) out vec4 fragmentColor;

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord/iResolution.xy;
    
    // Grab the pixel data from Buffer A
    vec4 col = texture(texMain, uv);

    // Apply the vignette effect
    float v = max(0.0, length(uv - 0.5) * 7.0 - 2.0);
    
    // FIXED: Replaced pow() with safe multiplication to prevent log(0) crashes
    vec3 safeCol = max(col.xyz, vec3(0.0));
    fragColor = vec4(safeCol * safeCol, 1.0);
}

void main() {
    mainImage(fragmentColor, gl_FragCoord.xy);
}