#version 400

uniform vec3 iResolution;
uniform float iTime;

in vec3 ray_dir, ray_origin;
layout(location = 0) out vec4 fragmentColor;


#define PI 3.1415926535f
float calculateDistanceToIntersection(
	out vec3 outNormal,
	const vec3 rayOrigin,
	const vec3 rayDirection,
	const float rayLength,
	const vec3 triangleVertex0,
	const vec3 triangleVertex1,
	const vec3 triangleVertex2){
	vec3 v01 = triangleVertex1 - triangleVertex0;
	vec3 v12 = triangleVertex2 - triangleVertex1;
	vec3 n = normalize(cross(v01, v12));
	float denom = dot(rayDirection, n);
	float t = dot(triangleVertex0 - rayOrigin, n) / denom;
	if(t < 0.0f || rayLength < t){
		return -1.0f;
	}
	vec3 x = rayOrigin + rayDirection * t;
	if(dot(cross(v01, x - triangleVertex0), n) < 0.0f){
		return -1.0f;
	}
	if(dot(cross(v12, x - triangleVertex1), n) < 0.0f){
		return -1.0f;
	}
	if(dot(cross(triangleVertex0 - triangleVertex2, x - triangleVertex2), n) < 0.0f){
		return -1.0f;
	}
	outNormal = n;
	return t;
}
vec3 vs[16];
uvec3 ts[18];
void traceRay_(
	out float outDistance,
	out vec3 outNormal,
	out vec3 outReflectance,
	out vec3 outEmission,
	const vec3 rayOrigin,
	const vec3 rayDirection){
	const float rayLength = 10000.0f;
	float closestD = rayLength;
	vec3 closestN = vec3(0.0f, 1.0f, 0.0f);
	for(int i = 0; i < 18; ++i){
		uvec3 t = ts[i];
		vec3 n;
		float d = calculateDistanceToIntersection(
        	n, rayOrigin, rayDirection, closestD, vs[t.x], vs[t.y], vs[t.z]);
		if(0.0f <= d){
			closestD = d;
			closestN = n;
		}
	}
	if(closestD < rayLength){
		outDistance = closestD;
		outNormal = (0.0f < dot(rayDirection, closestN)) ? -closestN : closestN;
		outReflectance = vec3(0.95f);
		outEmission = vec3(0.0f);
	}else{
		outDistance = -1.0f;
		outNormal = closestN;
		outReflectance = vec3(0.0f);
		//sky
		float t = (rayDirection.y + 1.0f) * 0.5f;
		outEmission = (1.0f - t) * vec3(1.0f) + t * vec3(0.25f, 0.49f, 1.0f);
        outEmission *= 2.0f;
	}
}
float calculateRandom(const vec2 uv, const vec2 offset){
	return fract(sin(dot(uv, vec2(12.9898f, 78.233f) + offset)) * 43758.5453123f);
}
vec3 calculateColor(
	const vec3 rayOrigin, const vec3 rayDirection, const int tryIndex,
    const vec2 uv)
{
	vec3 attenuation = vec3(1.0f);
	vec3 color = vec3(0.0f);
	vec3 ro = rayOrigin;
	vec3 rd = rayDirection;
	for(int i = 0; i < 4; ++i){
		float d;
		vec3 n;
		vec3 r;
		vec3 e;
		traceRay_(d, n, r, e, ro, rd);
		color += e * attenuation;
		attenuation *= r;
		if(d < 0.0f){
			break;
		}else{
			ro += rd * d;
			//diffuse
			{
				float r1 = 2.0f * PI * calculateRandom(uv, vec2(0.135f, -0.335f) * float(i + tryIndex));
				float r2 = calculateRandom(uv, vec2(0.397f, 0.7131f) * float(i * 3 + tryIndex * 5));
				float r2s = sqrt(r2);
				vec3 u;
				u = cross((0.1f < abs(n.x)) ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f), n);
				u = normalize(u);
				vec3 v;
				v = cross(n, u);
				vec3 d;
				d = u * cos(r1) * r2s;
				d += v * sin(r1) * r2s;
				d += n * sqrt(1.0f - r2);
				d = normalize(d);
				rd = d;
			}
			ro += rd * 0.001f;
		}
	}
	return color;
}
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 uv = vec3(fragCoord / iResolution.xy, 1.0f);
    // --- ALLOLIB INTEGRATION ---
    // Instead of calculating rayDirection from uv and hardcoding rayOrigin,
    // we use the actual 3D space coordinates pro
	vec3 rayDirection = normalize(ray_dir);
    vec3 rayOrigin = ray_origin;
    vec4 doorTransforms[2];
    int count = int(iTime * 10.0f);
    {	

        // Starts fully open (1.0). Waits 1 second, then smoothly closes between 1s and 3s.
        float rad = (1.0 - smoothstep(1.0, 7.0, iTime)) * PI;
        //float rad = (sin(float(count & 0x1ff) / 511.0f * PI * 4.0f) * 0.5f + 0.5f) * PI * 0.75f;
        float c = cos(rad);
        float s = sin(rad);
        vec3 p = vec3(5.0f, -0.1f, -12.549999f);
        doorTransforms[0] = vec4(c, 0.0f, s, -p.x * c - p.z * s + p.x);
        doorTransforms[1] = vec4(-s, 0.0f, c, p.x * s - p.z * c + p.z);
	}
	vs[0] = vec3(-5.0f, -0.10f, 5.0f);
	vs[1] = vec3(-5.0f, -0.10f, -15.0f);
	vs[2] = vec3(5.0f, -0.10f, 5.0f);
	vs[3] = vec3(5.0f, -0.10f, -15.0f);
	vs[4] = vec3(-5.0f, 3.90f, 5.0f);
	vs[5] = vec3(-5.0f, 3.90f, -15.0f);
	vs[6] = vec3(5.0f, 3.90f, 5.0f);
	vs[7] = vec3(5.0f, 3.90f, -15.0f);
	vs[8] = vec3(5.0f, -0.10f, -12.549999f);
	vs[9] = vec3(5.0f, -0.10f, -13.750f);
	vs[10] = vec3(5.0f, 1.90f, -12.549999f);
	vs[11] = vec3(5.0f, 1.90f, -13.750f);
	vs[12] = vec3(5.0f, -0.10f, -12.549999f);
	vs[13] = vec3(4.1514706f, -0.10f, -11.701471f);
	vs[14] = vec3(5.0f, 1.90f, -12.549999f);
	vs[15] = vec3(4.1514706f, 1.90f, -11.701471f);
	ts[0] = uvec3(6, 10, 7);
	ts[1] = uvec3(5, 4, 7);
	ts[2] = uvec3(0, 3, 1);
	ts[3] = uvec3(2, 3, 0);
	ts[4] = uvec3(7, 3, 5);
	ts[5] = uvec3(5, 1, 4);
	ts[6] = uvec3(7, 4, 6);
	ts[7] = uvec3(5, 3, 1);
	ts[8] = uvec3(4, 1, 0);
	ts[9] = uvec3(11, 9, 3);
	ts[10] = uvec3(7, 11, 3);
	ts[11] = uvec3(7, 10, 11);
	ts[12] = uvec3(10, 2, 8);
	ts[13] = uvec3(6, 2, 10);
	ts[14] = uvec3(14, 12, 15);
	ts[15] = uvec3(15, 12, 13);
	ts[16] = uvec3(4, 0, 6);
	ts[17] = uvec3(6, 0, 2);
	for(int i = 12; i <= 15; ++i){//rotate door
		vec4 t = vec4(vs[i - 4], 1.0f);
		vs[i].x = dot(doorTransforms[0], t);
		vs[i].z = dot(doorTransforms[1], t);
	}
	int spp = 16;
	vec3 c = vec3(0.0f);
	for(int i = 0; i < spp; ++i){
		c += calculateColor(rayOrigin, rayDirection, i + count, uv.xy);
	}
	fragColor.xyz = c * 1.0f / float(spp);
	fragColor.xyz = pow(fragColor.xyz, vec3(1.0f / 2.2f));
	float fadeOut = 1.0 - smoothstep(4.0, 6.0, iTime);
	fragColor.xyz *= fadeOut;
	fragColor.w = 1.0f;
}

void main() {
    mainImage(fragmentColor, gl_FragCoord.xy);
}