#version 430

// time from 0 to 1 modulus 1 second.
layout (location = 0) uniform float time;

// mouse in range x:[-1, 1] y:[-1, 1]
layout (location = 1) uniform vec2 mouse;

// screen coordinates in x:[-1, 1] y:[-1, 1]
in vec2 uv;

// output color
out vec4 color;

float gauss(float x, float x0, float sx) {
    float arg = x-x0;
    arg = -1./2.*arg*arg/sx;
    float a = 1./(pow(2.*3.1415*sx, 0.5));
    return a*exp(arg);
}

void main() {
  vec2 dim = vec2(4., 4.);
  vec2 ms_map = mouse * dim;
  vec2 uv_mod = mod(uv * dim, 1.);
  vec2 uv_int = floor(uv * dim) / (dim);
  
  float ms_d = gauss(distance(mouse, uv_int + vec2(1./16)), 0.1, 0.15); 
  float ms_f = .6 + ms_d * 2.5;
  float nv_d = distance(uv_mod, vec2(0.5));
  float nv_s = smoothstep(0.1, 0.13, pow(nv_d, ms_f));
  
  color = vec4(vec3(ms_d), 1.);
  //color = vec4(vec3(1. - nv_s), 1.);
}
