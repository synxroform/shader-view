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
  color = vec4(vec3(distance(uv, mouse)), 1.);
}
