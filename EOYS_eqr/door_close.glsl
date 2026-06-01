#version 400

uniform vec3 iResolution;
uniform float iTime;
uniform vec3 u_pos;
uniform vec4 u_quat;
uniform float eye_sep;
uniform float foc_len;

in vec2 v_texCoord;

layout(location = 0) out vec4 fragmentColor;

#define PI 3.1415926535f

// Cheap 2D Noise for the textured look and smoke
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898f, 78.233f))) * 43758.5453f);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0f - 2.0f * f);
    return mix(mix(hash(i + vec2(0.0f, 0.0f)), hash(i + vec2(1.0f, 0.0f)), u.x),
               mix(hash(i + vec2(0.0f, 1.0f)), hash(i + vec2(1.0f, 1.0f)), u.x), u.y);
}

// INVERSE Rotation Matrix
mat2 inverseRot(float a) {
    float s = sin(-a), c = cos(-a);
    return mat2(c, -s, s, c);
}

vec3 rotateVector(vec4 q, vec3 v) {
  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main() {
  float phi = -v_texCoord.x * 2.0 * PI;
  float theta = (v_texCoord.y - 0.5) * PI;

  vec3 rayDir;
  rayDir.x = cos(theta) * sin(phi);
  rayDir.y = sin(theta);
  rayDir.z = cos(theta) * cos(phi);

  rayDir = rotateVector(u_quat, rayDir);

  vec3 ro = u_pos;
  vec3 rd = normalize(rayDir);

    // --- ANIMATION TIMERS ---
     // 1. PHASED CLOSING: Starts at PI, pauses at 20 degrees, then finishes closing.
    float targetPauseAngle = 30.0f * PI / 180.0f; // 20 degrees in radians
    
    // Phase 1: Close from fully open down to 20 degrees (between 1.0s and 4.0s)
    float drop1 = (PI - targetPauseAngle) * smoothstep(0.5f, 2.5f, iTime); 
    
    // Phase 2: Close the final 20 degrees down to 0 (between 5.0s and 7.0s)
    float drop2 = targetPauseAngle * smoothstep(5.0f, 7.0f, iTime);        
    
    float rad = PI - drop1 - drop2; 
    
    // 2. Smoke kicks in right as the door finishes closing
    float smokeIntensity = smoothstep(5.5f, 4.70f, iTime);

    // 3. Fades the ENTIRE SCENE to black between 3.0s and 4.5s
    float globalFade = 1.0f - smoothstep(5.0f, 6.0f, iTime);

    // --- 1. THE VOID BACKGROUND ---
    float depthGlow = max(0.0f, rd.y * 0.5f + 0.5f);
    vec3 bgColor = mix(vec3(0.02f, 0.08f, 0.1f), vec3(0.05f, 0.25f, 0.25f), depthGlow);
    
    // Base murky water layer
    float n1 = noise(rd.xy * 8.0f + vec2(0.0f, iTime * 0.2f));
    bgColor += vec3(n1 * 0.05f);
    
    // Swirling smoke layer that rolls in as the door closes
    float n2 = noise(rd.xy * 12.0f + vec2(iTime * 0.1f, iTime * 0.4f));
    bgColor += vec3(n2 * 0.15f * smokeIntensity); 

    // --- 2. EXACT HINGE MATH ---
    vec3 hinge = vec3(5.0f, -0.1f, -12.549999f);
    vec3 localRo = ro - hinge;
    vec3 localRd = rd;

    // --- 3. PORTAL LIGHT SPILL ---
    // Calculate light escaping from the doorway behind the door
    float tPortal = -localRo.z / localRd.z;
    if (tPortal > 0.0f) {
        vec3 pPortal = localRo + localRd * tPortal;
        
        // Define the rectangular hole of the doorway (x: -1.2 to 0.0, y: 0.0 to 4.0)
        vec2 d = abs(pPortal.xy - vec2(-0.6f, 2.0f)) - vec2(0.6f, 2.0f);
        float distToPortal = length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
        
        // The light dynamically dims as the rad (angle) approaches 0
        float openFactor = smoothstep(0.1f, 1.2f, rad); 
        
        float glow = exp(-max(distToPortal, 0.0f) * 2.0f);
        
        if (distToPortal < 0.0f) glow = 1.5f; 
        
        float shimmer = noise(pPortal.xy * 3.0f + vec2(0.0f, -iTime * 0.8f));
        glow *= 0.6f + 0.4f * shimmer;
        
        vec3 lightCol = vec3(1.0f, 0.9f, 0.6f); 
        
        bgColor += lightCol * glow * openFactor;
    }

    // Apply the global fade to everything (water, smoke, AND the light)
    bgColor *= globalFade;
    vec3 col = bgColor;
    
    // Apply inverse rotation to the ray for the door math
    localRo.xz *= inverseRot(rad);
    localRd.xz *= inverseRot(rad);

    // --- 4. EXACT DOOR DIMENSIONS ---
    if (localRd.z != 0.0f) {
        float t = -localRo.z / localRd.z;

        if (t > 0.0f) {
            vec3 p = localRo + localRd * t;

            if (p.x > -1.2f && p.x < 0.0f && p.y > 0.0f && p.y < 4.0f) {
                
                if (globalFade > 0.0f) {
                    
                    vec2 uv = vec2((p.x + 1.2f) / 1.2f, p.y / 4.0f);

                    vec3 doorCol = vec3(0.7f, 0.1f, 0.1f);
                    float dirt = noise(uv * vec2(2.0f, 15.0f));
                    doorCol *= 0.6f + 0.4f * dirt;

                    float edgeX = min(uv.x, 1.0f - uv.x);
                    float edgeY = min(uv.y, 1.0f - uv.y);
                    if (min(edgeX, edgeY) < 0.05f) doorCol *= 0.3f; 
                    
                    if (length(uv - vec2(0.15f, 0.5f)) < 0.03f) doorCol = vec3(0.05f); 

                    // Shadow dynamically updates as the door closes
                    doorCol *= 0.6f + 0.4f * cos(rad);

                    float fog = clamp(t / 25.0f, 0.0f, 1.0f);
                    vec3 finalDoorCol = mix(doorCol, bgColor, fog);

                    col = mix(bgColor, finalDoorCol, globalFade);
                }
            }
        }
    }

    // --- 5. POST PROCESSING ---
    vec2 screenUv = gl_FragCoord.xy / iResolution.xy;
    col *= smoothstep(1.5f, 0.2f, length(screenUv - 0.5f));

    fragmentColor = vec4(col, 1.0f);
}