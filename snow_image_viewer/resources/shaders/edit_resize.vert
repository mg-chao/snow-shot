#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 uv;

layout(std140, binding = 0) uniform ResizeUniforms {
    mat4 mvp;
    vec4 sourceAndTarget;
    vec4 options;
    vec4 scaleAndAxis;
    ivec4 targetTile;
    vec4 backendParameters;
} uniforms;

void main()
{
    uv = texCoord;
    gl_Position = uniforms.mvp * vec4(position, 0.0, 1.0);
}
