#version 400

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_texCoord;

out vec2 v_texCoord;

void main() {
  v_texCoord = a_texCoord;
  gl_Position = vec4(a_position, 1.);

  // mat4 ivp = al_ModelMatrixInv * al_ViewMatrixInv * al_ProjMatrixInv;
  // vec4 worldspace_near = ivp * vec4(position.xy, -1., 1.);
  // vec4 worldspace_far = worldspace_near + ivp[2];
  // worldspace_far /= worldspace_far.w;
  // worldspace_near /= worldspace_near.w;
  // ray_dir = normalize(worldspace_far.xyz - worldspace_near.xyz);
  // ray_origin = worldspace_near.xyz;

  // // stereo offset:
  // // should reduce to zero as the nv becomes close to (0, 1, 0)
  // // take the vector of nv in the XZ plane
  // // and rotate it 90' around Y:
  // vec3 up = vec3(0, 1, 0);
  // vec3 rdx = cross(ray_dir, up);
  // vec3 eye_x = rdx * eye_sep;
  // ray_origin += eye_x;

  // // calculate new ray direction for positive parallax
  // ray_dir -= eye_x / foc_len;
  // ray_dir = normalize(ray_dir);
}