#version 430
#define PI 3.1415

// time from 0 to 1 modulus 1 second.
layout (location = 0) uniform float time;

// mouse in range x:[-1, 1] y:[-1, 1]
layout (location = 1) uniform vec2 mouse;

// size of the window in pixels.
layout (location = 3) uniform ivec2 size;

// screen coordinates in x:[-1, 1] y:[-1, 1]
in vec2 uv;

// output color
out vec4 color;

float rand(float n){return fract(sin(n) * 43758.5453123);}

float noise(float x) {
	float i = floor(x);
	float f = fract(x);
	float u = f * f * (3.0 - 2.0 * f);
	return mix(rand(i), rand(i + 1.0), u);
}

vec3 blend_screen(vec3 a, vec3 b) {
  vec3 white = vec3(1);
  return white - ((white - a) * (white - b));
}

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float circ_fn(float x) {
  return cos(asin(x));
}

float fluid(vec2 uv, float amp, float index) {
  float tt = time * 0.3;
  float na = pow(noise((uv.x + index + tt * abs(rand(index) + 0.5)) * 6), 5) * amp;
  float nb = pow(noise((uv.x + index + tt * 2) * 4), 2) * amp * 0.5;
  vec2 uva = uv + vec2(1, na);
  vec2 uvb = uv + vec2(1, nb);
  float a = smoothstep(0.9, 0.91, uva.y);
  float b = 1 - smoothstep(0., 0.03, distance(0.7, uvb.y));
  return a + b; 
}

vec4 bubble(vec2 pos) {
  float y_mask = float(pos.y > 0);
  float h_xpos = (pos.x + 1) * 0.5;
  float h_grad = 1 - smoothstep(0, 1, abs(pos.x));
  float y_wave = mod(pos.y*8 - (h_grad - time), 1); 
  float index = floor(pos.y*8 - (h_grad - time));
  float flow = fluid(vec2(h_xpos, y_wave), 0.5, index);
  
  return vec4(vec3(flow), 1);
}

void main() {
  // pixel size in units
  float pixel_size = 1.0 / (size.x * 0.5);
  float sample_step = pixel_size / 4;
  
  // simple 4x4 antialiasing works well
  vec4 acc = vec4(0);
  for (int j=0; j < 4; j++)
  for (int i=0; i < 4; i++) {
    vec2 uv_samp = uv + vec2(sample_step) * vec2(i, j); 
    acc += bubble(fma(uv_samp, vec2(1, 0.5), vec2(0, 0.3)));
  }
  color = acc / 16;
}

// 450,582
