#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 shape;
layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec4 vertexShape;

layout(std140, binding = 0) uniform NavigationUniforms {
    mat4 mvp;
} uniforms;

void main()
{
    vertexColor = color;
    vertexShape = shape;
    gl_Position = uniforms.mvp * vec4(position, 0.0, 1.0);
}
