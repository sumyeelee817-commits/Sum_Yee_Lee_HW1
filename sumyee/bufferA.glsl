#version 400
uniform vec3 iResolution;
uniform float iTime;
uniform sampler2D texFeedback; // Catches our previous frame memory

layout(location = 0) out vec4 fragmentColor;
in vec3 ray_dir, ray_origin;

// --- COMMON CODE ---
#define PI 3.14159265359
#define HALFPI 1.57079632679
#define HASHSCALE1 .1031
#define HASHSCALE3 vec3(.1031, .1030, .0973)
#define HASHSCALE4 vec4(.1031, .1030, .0973, .1099)

vec2 hash21(float p) {
    vec3 p3 = fract(vec3(p) * HASHSCALE3);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.xx+p3.yz)*p3.zy);
}

vec4 hash42(vec2 p) {
    vec4 p4 = fract(vec4(p.xyxy) * HASHSCALE4);
    p4 += dot(p4, p4.wzxy+19.19);
    return fract((p4.xxyz+p4.yzzw)*p4.zywx);
}

mat2 rot(float a) {
    vec2 s = sin(vec2(a, a + HALFPI));
    return mat2(s.y,s.x,-s.x,s.y);
}

vec2 CalculateUv( vec2 coord, float time) {
    vec2 uv = coord;
    uv *= rot(time*0.1);
    uv += sin(vec2(time*0.2, time*0.3 + HALFPI)) * 0.35;
    vec4 disto = sin(uv.xxyy * vec4(8.1, 7.8, 7.7, 8.3) + vec4(0.3, -0.4, 0.25, -0.3) * time) * vec4(0.01, 0.015, 0.007, 0.012);
    uv.x += disto.z + disto.w;
    uv.y += disto.x + disto.y;
    return uv;
}

// --- PROCEDURAL NOISE (Replaces iChannel1 & iChannel2) ---
float random(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123); }
float noise(vec2 st) {
    vec2 i = floor(st); vec2 f = fract(st);
    float a = random(i); float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0)); float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a)* u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// --- BUFFER A MAIN RENDERING ---
float star(vec2 uv, vec2 s, vec2 offset) {
    uv += offset;
    uv *= 2.0;
    float l = length(uv);
    l = sqrt(l);
    // FIXED: Mac-compliant smoothstep interpolation
    vec2 v = 1.0 - smoothstep(vec2(0.0), s, vec2(l));
    return v.x + v.y*0.1;
}

vec4 starField(vec2 uv) {
    vec2 fracuv = fract(uv);
    vec2 flooruv = floor(uv);
    vec4 r = hash42(flooruv);
    vec4 color = mix(vec4(0.5, 1.0, 0.25, 1.0), vec4(0.25, 0.5, 1.0, 1.0), dot(r.xy, r.zw)) * 4.0 * dot(r.xz, r.yw);
    float t = iTime*2.0;
    vec2 o = sin(vec2(t, t + HALFPI) * r.yx) * r.zw * 0.75;
    return color * star((fracuv - 0.5) * 2.0, vec2(0.4, 0.75) * (0.5 + 0.5*r.xy), o);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 uv = fragCoord/iResolution.xy;
    uv -= 0.5;
    uv.x *= iResolution.x/iResolution.y;
    uv -= ray_dir.xy * 0.6;
    uv = CalculateUv(uv, iTime);
        
    vec4 res = vec4(0.0);
    float t = (iTime * -0.075) + (ray_origin.z * 0.04);
    const float itter = 15.0;
    float tex = 0.0;
    vec2 disto = vec2(0.0);
    
    for(float f = 0.0; f < itter; f++) {
        vec2 r = hash21(f);
        float t2 = fract(t + f / itter);
        float size = mix(30.0, 0.001, t2);
        
        // FIXED: Separated channels to prevent backwards vector interpolation
        float fadeX = smoothstep(0.0, 0.9, t2);
        float fadeY = 1.0 - smoothstep(0.9, 1.0, t2);
        vec2 fade = vec2(fadeX, fadeY);
        
        vec2 uv2 = uv * size + r * 100.0 + (r - 0.5) * iTime * 0.25;
        uv2 -= ray_origin.xy * size * 0.03;
        res += starField(uv2) * fade.x * fade.y * 0.65;
        tex = noise(uv2*1.0 + tex * 1.5);
        disto += vec2(tex) / itter;
        res += tex * tex * 3.0 * fade.x * fade.y * vec4(0.25, 0.35, 0.5, 1.0) / itter;
    }
    
    vec2 distuv = uv + disto*0.15;
    
    // FIXED: Manually separate the sun channels to prevent macOS vector math crashes
    float distSq = dot(distuv, distuv);
    vec4 sun;
    sun.x = 1.0 - smoothstep(0.0, 0.25, distSq);
    sun.y = 1.0 - smoothstep(0.0, 3.0, distSq);
    sun.z = 1.0 - smoothstep(0.0, 0.0025, distSq);
    sun.w = 1.0 - smoothstep(0.0, 4.5, distSq);
    
    sun = sun * sun * sun * sun;
    sun.xyz *= 0.5;
    
    vec2 pc;
    pc.x = iTime*0.02;
    // FIXED: Add tiny offset to prevent atan(0,0) returning NaN
    pc.y = (atan(distuv.x, distuv.y + 1e-6) / PI) + iTime * 0.05;
    
    float rays = (noise(pc * 10.0) + noise(pc * vec2(10.0, 5.0))) * sun.y * 0.5;
    
    vec4 bg = mix(vec4(0.01, 0.01, 0.075, 1.0), vec4(0.05, 0.065, 0.1, 1.0), clamp(sun.x + rays, 0.0, 1.0) + sun.y);
    bg += sun.z*vec4(0.2, 0.2, 0.05, 0.0) + sun.x * vec4(0.05, 0.05, 0.075, 0.0);
    
    vec4 c = bg + res * bg * 0.5 + res*0.5;
    c *= sun.w;
    
    vec4 old = clamp(texture(texFeedback, fragCoord/iResolution.xy), 0.0, 1.0);
    fragColor = c + old * 0.85;
}

void main() {
    mainImage(fragmentColor, gl_FragCoord.xy);
}