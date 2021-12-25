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

mat2 rotate(float a) {
  return mat2 (
    cos(a), -sin(a),
    sin(a), cos(a)
  );
}

float square(float x) {
  return smoothstep(0.5, 0.51, mod(x, 1));
}

float jelly(vec2 uv) {
  vec2 t_uv = uv + vec2(1, square((uv.x) * 100) * sin((uv.x + time * 0.4) * 20) * 0.1);
  float a = (1 - smoothstep(0.1 * uv.x, 0.15 * uv.x , distance(t_uv.y, 0.5))) * pow(uv.x, 5);
  float b = (1 - smoothstep(0.3, 0.33, distance(vec2(uv.x * 4, uv.y * 0.8), vec2(0, 0.4)))) * 0.5;
  float c = 1 - smoothstep(0.3, 0.33, distance(vec2(uv.x * 6, uv.y), vec2(0, 0.5)));
  return a + b + c; 
}

vec4 wave(vec2 pos) {
  float tt = time * 0.3 + 5;
  float h_xpos = (pos.x + 1) * 0.5;
  float h_grad = sin((pos.x + sin(time)) *  PI) * 0.3 * sin(pos.y * 5);
  float index = floor(pos.y * 4 - (h_grad - time));
  float direction = bool(mod(index, 2)) ? 1 : -1;
  float t_grad = abs(mod(index, 2) - mod(h_xpos + noise(index) * tt * direction, 1));
  float y_wave = mod(pos.y * 4 - (h_grad - time), 1); 
  
  float flow = jelly(vec2(t_grad, y_wave));
  
  float cm = noise(index);
  vec4 c1 = vec4(1.000, 0.090, 0.549, 1.0);
  vec4 c2 = vec4(0.114, 0.800, 1.000, 1.0);
  vec4 cc = c1 * cm + c2 * (1 - cm);
  
  return vec4(vec3(flow), 1) * cc;
}

void main() {
  color = wave(rotate(radians(45)) * uv);
}
