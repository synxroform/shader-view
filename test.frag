#version 430

// time from 0 to 1 modulus 1 second.
layout (location = 0) uniform float time;

// mouse in range x:[-1, 1] y:[-1, 1]
layout (location = 1) uniform vec2 mouse;

// screen coordinates in x:[-1, 1] y:[-1, 1]
in vec2 uv;

// output color
out vec4 color;

void main() {
  vec2 uv_mod = mod(uv * 15, 1.);
  vec2 uv_int = floor(uv * 80);
  float d = 1. - smoothstep(0.1, 0.5, distance(vec2(0.5), uv_mod));
  color = vec4(vec3(d), 1.);
}
