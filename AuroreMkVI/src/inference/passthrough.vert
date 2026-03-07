#version 310 es

precision highp float;
layout(location = 0) in vec4 a_position;
void main() {
    gl_Position = a_position;
}
