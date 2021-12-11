#version 430
#define PI 3.1415

// time from 0 to 1 modulus 1 second.
layout (location = 0) uniform float time;

// mouse in range x:[-1, 1] y:[-1, 1]
layout (location = 1) uniform vec2 mouse;

// screen coordinates in x:[-1, 1] y:[-1, 1]
in vec2 uv;

// output color
out vec4 color;

float rand(float n){return fract(sin(n) * 43758.5453123);}

mat2 rotate(float a) {
  return mat2 (
    cos(a), -sin(a),
    sin(a), cos(a)
  );
}

vec2 c2p(vec2 pt) {
  return vec2(sqrt(pow(pt.x, 2) + pow(pt.y, 2)), atan(pt.y, pt.x));
}

float domain(vec2 uv, vec2 h, vec2 v, vec2 s) {
  vec2 sh = h + vec2(s.x);
  vec2 sv = v + vec2(s.y);
  float dh = smoothstep(h.x, sh.x, uv.x) * (1 - smoothstep(h.y, sh.y, uv.x));
  float dv = smoothstep(v.x, sv.x, uv.y) * (1 - smoothstep(v.y, sv.y, uv.y));
  return dh * dv;
}


void main() {
  vec2 origin = vec2(0.25, 0.5);
  
  float fx = time * 0.8;
  float d0 = distance(uv - origin, vec2(0.)) * 8;
  float r0 = rand(floor(d0));
  float mm = smoothstep(2., 2.1, d0);
  
  vec2 p0 = c2p(rotate(r0 * 10 * (1 + fx)) * (uv - origin)) / vec2(1, PI);
  vec4 c0 = vec4(0.047, 0.820, 1.000, 1.0);
  vec4 c1 = vec4(1.000, 0.090, 0.710, 1.0);
  vec4 cc = c0 * r0 + c1 * (1 - r0);
  vec2 dd = vec2(pow(mod(d0, 1), 2 + sin(p0.y * 20 + fx * 50) * (1-p0.y) * 2), p0.y);
  
  float l1 = domain(dd,  vec2(0.3, 0.5), vec2(0.1, 0.9), vec2(0.1, 0.005));
  float l2 = domain(dd,  vec2(0.1, 0.8), vec2(0.6, 0.74), vec2(0.1, 0.2));
  
  color = vec4(vec3((l1 + l2) * p0.y * mm), 1.) * cc;
}


