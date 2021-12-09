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
  vec2 n_ms = (mouse + 1) * 0.5;
  vec2 dim = vec2(8., 8.);
  vec2 uv_mod = mod(uv * dim, 1.);
  vec2 uv_int = floor((uv + 1) * dim) / (dim * 2);
  float d1 = 1. - smoothstep(0.01, 0.1, distance(vec2(0.5), uv_mod));
  float d2 = distance(n_ms, uv_int + vec2(.5)); 
  float d3 = distance(mouse, uv);
  color = vec4(vec3(1. - d3) * vec3(0.1, 0.5, 0.7), 1.);
}
