#version 400

uniform vec3 iResolution;
uniform float iTime;
uniform vec3 u_pos;
uniform vec4 u_quat;
uniform float eye_sep;
uniform float foc_len;

in vec2 v_texCoord;

layout(location = 0) out vec4 frag_out0;

#define PI 3.1415926535f

// Cheap 2D Noise for the textured look and smoke
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
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
    // 1. Opens smoothly once between 1.0s and 3.0s
  float rad = smoothstep(1.0, 3.0, iTime) * PI; 

    // 2. Smoke kicks in right as the door finishes opening
  float smokeIntensity = smoothstep(2.5, 4.0, iTime);

    // 3. Fades the ENTIRE SCENE to black between 3.0s and 4.5s
  float globalFade = 1.0 - smoothstep(3.0, 4.5, iTime);

    // --- 1. THE VOID BACKGROUND ---
  float depthGlow = max(0.0, rd.y * 0.5 + 0.5);
  vec3 bgColor = mix(vec3(0.02, 0.08, 0.1), vec3(0.05, 0.25, 0.25), depthGlow);

    // Base murky water layer
  float n1 = noise(rd.xy * 8.0 + vec2(0.0, iTime * 0.2));
  bgColor += vec3(n1 * 0.05);

    // Swirling smoke layer that rolls in as the door opens
  float n2 = noise(rd.xy * 12.0 + vec2(iTime * 0.1, iTime * 0.4));
  bgColor += vec3(n2 * 0.15 * smokeIntensity); 

    // --- 2. EXACT HINGE MATH ---
  vec3 hinge = vec3(5.0, -0.1, -12.549999);
  vec3 localRo = ro - hinge;
  vec3 localRd = rd;

    // --- NEW: PORTAL LIGHT SPILL ---
    // Calculate light escaping from the doorway behind the door
  float tPortal = -localRo.z / localRd.z;
  if(tPortal > 0.0) {
    vec3 pPortal = localRo + localRd * tPortal;

        // Define the rectangular hole of the doorway (x: -1.2 to 0.0, y: 0.0 to 4.0)
        // Center X is now -0.6, Half-width is 0.6
    vec2 d = abs(pPortal.xy - vec2(-0.6, 2.0)) - vec2(0.6, 2.0);
    float distToPortal = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);

        // The light gets brighter as the door opens wider
    float openFactor = smoothstep(0.1, 1.2, rad); 

        // Exponential falloff creates a soft, volumetric "glow" bleeding into the water
    float glow = exp(-max(distToPortal, 0.0) * 2.0);

        // If the ray is looking directly inside the open portal, make it blindingly bright
    if(distToPortal < 0.0)
      glow = 1.5; 

        // Add a shimmering "god ray" effect using the noise function
    float shimmer = noise(pPortal.xy * 3.0 + vec2(0.0, -iTime * 0.8));
    glow *= 0.6 + 0.4 * shimmer;

        // Warm, ethereal golden/white light
    vec3 lightCol = vec3(1.0, 0.9, 0.6); 

        // Add the light to the murky background
    bgColor += lightCol * glow * openFactor;
  }

    // Apply the global fade to everything (water, smoke, AND the new light)
  bgColor *= globalFade;
  vec3 col = bgColor;

    // Apply inverse rotation to the ray for the door math
  localRo.xz *= inverseRot(rad);
  localRd.xz *= inverseRot(rad);

    // --- 3. EXACT DOOR DIMENSIONS ---
  if(localRd.z != 0.0) {
    float t = -localRo.z / localRd.z;

    if(t > 0.0) {
      vec3 p = localRo + localRd * t;

            // WIDENED DOOR: spans from -1.2 to 0.0 on the X axis
      if(p.x > -1.2 && p.x < 0.0 && p.y > 0.0 && p.y < 4.0) {

                // Only calculate and draw the door if it hasn't completely faded into darkness yet
        if(globalFade > 0.0) {

                    // UPDATED UV MATH: Divide by the new width (1.2)
          vec2 uv = vec2((p.x + 1.2) / 1.2, p.y / 4.0);

                    // Base weathered red color
          vec3 doorCol = vec3(0.7, 0.1, 0.1);
          float dirt = noise(uv * vec2(2.0, 15.0));
          doorCol *= 0.6 + 0.4 * dirt;

                    // Faked Panel Carvings
          float edgeX = min(uv.x, 1.0 - uv.x);
          float edgeY = min(uv.y, 1.0 - uv.y);
          if(min(edgeX, edgeY) < 0.05)
            doorCol *= 0.3; 

                    // Doorknob
          if(length(uv - vec2(0.15, 0.5)) < 0.03)
            doorCol = vec3(0.05); 

                    // Simulate shadow dynamically turning as the door opens
          doorCol *= 0.6 + 0.4 * cos(rad);

                    // Distance Fog
          float fog = clamp(t / 25.0, 0.0, 1.0);
          vec3 finalDoorCol = mix(doorCol, bgColor, fog);

                    // Smoothly blend the door into the fading void
          col = mix(bgColor, finalDoorCol, globalFade);
        }
      }
    }
  }

    // --- 4. POST PROCESSING ---
  vec2 screenUv = gl_FragCoord.xy / iResolution.xy;
  col *= smoothstep(1.5, 0.2, length(screenUv - 0.5));

  frag_out0 = vec4(col, 1.0);
}