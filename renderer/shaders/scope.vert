#version 450
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 background;
layout(location = 0) out vec2 texcoord;
layout(location = 1) out vec4 foreground;
layout(location = 2) out vec4 backdrop;
void main() {
    gl_Position = position;
    texcoord = uv;
    foreground = color;
    backdrop = background;
}
